#define LOG_TAG "AA.Session"

#include "aauto/session/Session.hpp"
#include "aauto/utils/Logger.hpp"
#include "aauto/utils/ProtobufCompat.hpp"
#include "aauto/utils/ProtocolUtil.hpp"

#include <aap_protobuf/service/control/message/AuthResponse.pb.h>

#include <chrono>

namespace aauto::session {

namespace pb_ctrl = aap_protobuf::service::control::message;

// Helper: serialize protobuf to byte vector
template <typename T>
static std::vector<uint8_t> serialize(const T& msg) {
    return utils::serialize_to_vector(msg);
}

Session::Session(asio::any_io_executor executor,
                 SessionConfig config,
                 std::shared_ptr<transport::ITransport> transport,
                 std::shared_ptr<crypto::ICryptoStrategy> crypto,
                 ISessionObserver* observer)
    : strand_(asio::make_strand(executor))
    , config_(config)
    , transport_(std::move(transport))
    , crypto_(std::move(crypto))
    , observer_(observer)
    // Wrap all callbacks with bind_executor(strand_) to ensure crypto
    // async callbacks re-enter on the strand, preventing data races.
    , outbound_encoder_(
        crypto_,
        asio::bind_executor(strand_,
            [this](const std::error_code& ec) { handle_error(ec); }),
        asio::bind_executor(strand_,
            [this](std::vector<uint8_t> wire) { enqueue_write(std::move(wire)); }))
    , inbound_assembler_(
        crypto_,
        asio::bind_executor(strand_,
            [this](const std::error_code& ec) { handle_error(ec); }))
    , handshake_coordinator_(
        crypto_,
        asio::bind_executor(strand_,
            [this](uint16_t type, const std::vector<uint8_t>& payload) {
                send_message(kControlChannelId, type, payload);
            }),
        asio::bind_executor(strand_,
            [this] { on_ssl_complete(); }),
        asio::bind_executor(strand_,
            [this](const std::error_code& ec) { handle_error(ec); }))
    , state_timer_(strand_) {
    read_buffer_.fill(0);
}

Session::~Session() {
    AA_LOG_D("session %u destroyed", config_.session_id);
}

SessionState Session::state() const {
    return state_;
}

// ===== Public API =====

void Session::start() {
    session_tag_ = "s" + std::to_string(config_.session_id);
    auto self = shared_from_this();
    asio::post(strand_, [self] {
        self->activate_log_tag();
        self->begin_version_exchange();
    });
}

void Session::activate_log_tag() {
    set_session_tag(session_tag_);
}

void Session::update_log_tag(const std::string& suffix) {
    session_tag_ = "s" + std::to_string(config_.session_id) + ":" + suffix;
    activate_log_tag();
}

void Session::stop() {
    auto self = shared_from_this();
    asio::post(strand_, [self] {
        if (is_terminal(self->state_)) return;

        if (self->state_ == SessionState::Running) {
            self->begin_disconnect();
        } else {
            self->handle_error(make_error_code(AapErrc::SessionTerminated));
        }
    });
}

void Session::register_service(uint8_t channel_id,
                               std::shared_ptr<service::IService> svc) {
    services_[channel_id] = std::move(svc);
}

void Session::set_video_surface(void* native_window) {
    // Broadcast to all services; only VideoService overrides set_native_window.
    for (auto& [ch, svc] : services_) {
        svc->set_native_window(native_window);
    }
}

void Session::set_video_focus(bool projected) {
    // Only video service handles focus — audio sinks unaffected
    for (auto& [ch, svc] : services_) {
        svc->set_video_focus(projected);
    }
}

void Session::attach_all_sinks() {
    for (auto& [ch, svc] : services_) {
        svc->attach_sinks();
    }
}

void Session::detach_all_sinks() {
    for (auto& [ch, svc] : services_) {
        svc->detach_sinks();
    }
}

void Session::send_touch_event(int32_t x, int32_t y, int32_t action) {
    for (auto& [ch, svc] : services_) {
        svc->send_touch(x, y, action);
    }
}

// ===== Send =====

void Session::send_message(uint8_t channel_id, uint16_t message_type,
                           const std::vector<uint8_t>& payload) {
    outbound_encoder_.send_message(channel_id, message_type, payload);
}

void Session::send_raw(uint8_t channel_id, uint16_t message_type,
                       const uint8_t* data, std::size_t size) {
    std::vector<uint8_t> payload(data, data + size);
    send_message(channel_id, message_type, payload);
}

// ===== Write queue =====

void Session::enqueue_write(std::vector<uint8_t> wire_data) {
    if (write_queue_.size() >= kMaxWriteQueueSize) {
        AA_LOG_W("write queue full (%zu), dropping frame",
                 write_queue_.size());
        return;
    }
    write_queue_.push(std::move(wire_data));
    if (!write_in_progress_) {
        do_write_next();
    }
}

void Session::do_write_next() {
    if (write_queue_.empty()) {
        write_in_progress_ = false;
        return;
    }

    write_in_progress_ = true;
    auto& front = write_queue_.front();
    auto self = shared_from_this();
    transport_->async_write(asio::buffer(front),
        [self](const std::error_code& ec, std::size_t bytes) {
            self->on_write_complete(ec, bytes);
        });
}

void Session::on_write_complete(const std::error_code& ec,
                                std::size_t /*bytes*/) {
    activate_log_tag();
    if (ec) {
        if (ec == asio::error::operation_aborted) return;
        AA_LOG_E("write error: %s", ec.message().c_str());
        handle_error(make_error_code(AapErrc::TransportWriteError));
        return;
    }

    write_queue_.pop();
    do_write_next();
}

// ===== Read loop =====

void Session::start_read() {
    if (is_terminal(state_)) return;

    auto self = shared_from_this();
    transport_->async_read(asio::buffer(read_buffer_),
        [self](const std::error_code& ec, std::size_t bytes) {
            self->on_read_complete(ec, bytes);
        });
}

void Session::on_read_complete(const std::error_code& ec,
                               std::size_t bytes) {
    activate_log_tag();
    if (ec) {
        if (ec == asio::error::operation_aborted) return;
        AA_LOG_E("read error: %s", ec.message().c_str());
        handle_error(make_error_code(AapErrc::TransportReadError));
        return;
    }

    auto self = shared_from_this();
    inbound_assembler_.feed(read_buffer_.data(), bytes,
        [self](AapMessage message) {
            self->dispatch_decrypted(message.channel_id,
                                     message.message_type,
                                     std::move(message.payload));
        });

    start_read();
}

bool Session::handle_handshake_message(uint16_t msg_type,
                                       const std::vector<uint8_t>& payload) {
    auto ct = static_cast<ControlMessageType>(msg_type);
    switch (ct) {
        case ControlMessageType::VersionResponse:
            on_version_response(payload);
            return true;
        case ControlMessageType::EncapsulatedSsl:
            handshake_coordinator_.on_ssl_data_received(payload.data(), payload.size());
            return true;
        default:
            AA_LOG_W("unexpected %s during handshake", msg_type_name(msg_type));
            return true;
    }
}

bool Session::is_handshake_state() const {
    return state_ == SessionState::VersionExchange ||
           state_ == SessionState::SslHandshake;
}

void Session::dispatch_decrypted(uint8_t channel_id, uint16_t msg_type,
                                 std::vector<uint8_t> payload) {
    // Suppress noisy per-message logs (media data, ping)
    if (msg_type != static_cast<uint16_t>(MediaMessageType::Data)
            && msg_type != static_cast<uint16_t>(ControlMessageType::PingRequest)
            && msg_type != static_cast<uint16_t>(ControlMessageType::PingResponse)) {
        AA_LOG_D("rx [%s] %s (%zu bytes)",
                 channel_name(channel_id), msg_type_name(msg_type),
                 payload.size());
    }

    // During handshake, Session handles VERSION and SSL messages directly.
    // After handshake (Running), ALL messages go to registered services.
    if (is_handshake_state()) {
        handle_handshake_message(msg_type, payload);
        return;
    }

    // Post-handshake: delegate to services (including ch 0 = ControlService)
    auto it = services_.find(channel_id);
    if (it != services_.end()) {
        it->second->on_message(msg_type, payload.data(), payload.size());
    } else {
        AA_LOG_W("no service for [%s] %s", channel_name(channel_id),
                 msg_type_name(msg_type));
    }
}

// ===== State machine =====

void Session::transition_to(SessionState new_state) {
    AA_LOG_I("session %u: %s -> %s", config_.session_id,
             to_string(state_), to_string(new_state));
    state_ = new_state;
    state_timer_.cancel();

    if (observer_) {
        observer_->on_session_state_changed(config_.session_id, state_);
    }
}

void Session::close_transport_and_services() {
    state_timer_.cancel();
    transport_->close();

    for (auto& [ch_id, svc] : services_) {
        (void)ch_id;
        svc->on_channel_close();
    }
}

void Session::handle_error(const std::error_code& ec) {
    if (is_terminal(state_)) return;

    AA_LOG_E("session %u error: %s", config_.session_id, ec.message().c_str());
    transition_to(SessionState::Error);
    close_transport_and_services();

    if (observer_) {
        observer_->on_session_error(config_.session_id, ec);
    }
}

void Session::start_state_timer(uint32_t timeout_ms) {
    state_timer_.expires_after(std::chrono::milliseconds(timeout_ms));
    auto self = shared_from_this();
    state_timer_.async_wait([self](const std::error_code& ec) {
        if (!ec) {
            self->on_state_timeout();
        }
    });
}

void Session::on_state_timeout() {
    AA_LOG_W("session %u: state timeout in %s", config_.session_id,
             to_string(state_));

    switch (state_) {
        case SessionState::VersionExchange:
            handle_error(make_error_code(AapErrc::VersionMismatch));
            break;
        case SessionState::SslHandshake:
            handle_error(make_error_code(AapErrc::SslHandshakeFailed));
            break;
        case SessionState::Disconnecting:
            complete_disconnect();
            break;
        default:
            break;
    }
}

// ===== Handshake sequence =====
// AAP protocol order: VERSION → SSL → AUTH → (Running: services take over)

void Session::begin_version_exchange() {
    transition_to(SessionState::VersionExchange);
    start_state_timer(config_.version_exchange_timeout_ms);
    start_read();
    send_version_request();
}

void Session::begin_disconnect() {
    transition_to(SessionState::Disconnecting);
    start_state_timer(config_.byebye_timeout_ms);

    if (services_.find(kControlChannelId) == services_.end()) {
        complete_disconnect();
        return;
    }

    for (auto& [ch, svc] : services_) {
        (void)ch;
        svc->on_session_stop();
    }
}

void Session::send_version_request() {
    // VERSION_REQUEST payload: raw bytes [major:2 BE][minor:2 BE], not protobuf.
    // v1.1 — matches reference headunit. Phone responds with its version.
    std::vector<uint8_t> payload = {0, 1, 0, 1};
    send_message(kControlChannelId,
        static_cast<uint16_t>(ControlMessageType::VersionRequest),
        payload);
    AA_LOG_D("sent VERSION_REQUEST (v1.1)");
}

void Session::on_version_response(const std::vector<uint8_t>& payload) {
    transition_to(SessionState::SslHandshake);
    start_state_timer(config_.ssl_handshake_timeout_ms);
    handshake_coordinator_.on_version_response(payload);
}

void Session::on_ssl_complete() {
    AA_LOG_I("SSL handshake complete, sending AUTH_COMPLETE (plaintext)");

    // AUTH_COMPLETE must be sent UNENCRYPTED — it's the last plaintext message.
    // After this message, all subsequent messages will be encrypted.
    pb_ctrl::AuthResponse auth;
    auth.set_status(0);
    auto payload = serialize(auth);
    outbound_encoder_.send_plaintext_control_message(
        static_cast<uint16_t>(ControlMessageType::AuthComplete), payload);

    // Handshake complete — transition to Running.
    // ControlService will handle ServiceDiscovery, ChannelOpen, Ping, etc.
    transition_to(SessionState::Running);
    AA_LOG_I("session running, all messages delegated to services");
}

void Session::complete_disconnect() {
    transition_to(SessionState::Disconnected);
    transport_->close();
}

} // namespace aauto::session
