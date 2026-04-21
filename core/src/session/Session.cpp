#define LOG_TAG "AA.Session"

#include "aauto/session/Session.hpp"
#include "aauto/utils/Logger.hpp"
#include "aauto/utils/ProtocolUtil.hpp"

#include <aap_protobuf/service/control/message/AuthResponse.pb.h>

#include <chrono>

namespace aauto::session {

namespace pb_ctrl = aap_protobuf::service::control::message;

// Helper: serialize protobuf to byte vector
static std::vector<uint8_t> serialize(const google::protobuf::MessageLite& msg) {
    std::vector<uint8_t> buf(msg.ByteSize());
    msg.SerializeToArray(buf.data(), static_cast<int>(buf.size()));
    return buf;
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
    auto self = shared_from_this();
    asio::post(strand_, [self] {
        set_session_tag("s" + std::to_string(self->config_.session_id));
        self->begin_version_exchange();
    });
}

void Session::stop() {
    auto self = shared_from_this();
    asio::post(strand_, [self] {
        if (is_terminal(self->state_)) return;

        if (self->state_ == SessionState::Running) {
            self->transition_to(SessionState::Disconnecting);
            self->start_state_timer(self->config_.byebye_timeout_ms);

            for (auto& [ch, svc] : self->services_) {
                svc->on_session_stop();
            }
        } else {
            self->handle_error(make_error_code(AapErrc::SessionTerminated));
        }
    });
}

void Session::register_service(uint8_t channel_id,
                               std::shared_ptr<service::IService> svc) {
    services_[channel_id] = std::move(svc);
}

// ===== Send =====

void Session::send_message(uint8_t channel_id, uint16_t message_type,
                           const std::vector<uint8_t>& payload) {
    // Build full payload: [message_type:2 BE][protobuf body]
    std::vector<uint8_t> full_payload;
    full_payload.reserve(2 + payload.size());
    full_payload.push_back(static_cast<uint8_t>((message_type >> 8) & 0xFF));
    full_payload.push_back(static_cast<uint8_t>(message_type & 0xFF));
    full_payload.insert(full_payload.end(), payload.begin(), payload.end());

    // After SSL handshake, ALL channels are encrypted (including control)
    bool needs_encryption = crypto_->is_established();

    if (needs_encryption) {
        auto self = shared_from_this();
        auto flags = compute_frame_flags(channel_id, message_type, true);
        crypto_->encrypt(full_payload.data(), full_payload.size(),
            [self, channel_id, flags](const std::error_code& ec,
                               std::vector<uint8_t> ciphertext) {
                if (ec) {
                    AA_LOG_E("encrypt failed: %s", ec.message().c_str());
                    self->handle_error(ec);
                    return;
                }
                OutboundFrame frame{channel_id, flags, std::move(ciphertext)};
                auto wire_frames = self->framer_.encode(frame);
                for (auto& wire : wire_frames) {
                    self->enqueue_write(std::move(wire));
                }
            });
    } else {
        auto flags = compute_frame_flags(channel_id, message_type, false);
        OutboundFrame frame{channel_id, flags, std::move(full_payload)};
        auto wire_frames = framer_.encode(frame);
        for (auto& wire : wire_frames) {
            enqueue_write(std::move(wire));
        }
    }
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
    if (ec) {
        if (ec == asio::error::operation_aborted) return;
        AA_LOG_E("read error: %s", ec.message().c_str());
        handle_error(make_error_code(AapErrc::TransportReadError));
        return;
    }

    auto self = shared_from_this();
    framer_.feed(read_buffer_.data(), bytes,
        [self](const std::error_code& frame_ec, AapFrame frame) {
            if (frame_ec) {
                AA_LOG_E("framing error: %s", frame_ec.message().c_str());
                self->handle_error(frame_ec);
                return;
            }
            self->dispatch_frame(std::move(frame));
        });

    start_read();
}

void Session::dispatch_frame(AapFrame frame) {

    if (frame.encrypted && crypto_->is_established()) {
        auto self = shared_from_this();
        auto channel_id = frame.channel_id;

        // Reconstruct full ciphertext: Framer stripped 2 bytes as message_type,
        // but for encrypted frames, those bytes are part of the ciphertext.
        std::vector<uint8_t> ciphertext;
        ciphertext.reserve(2 + frame.payload.size());
        ciphertext.push_back(static_cast<uint8_t>((frame.message_type >> 8) & 0xFF));
        ciphertext.push_back(static_cast<uint8_t>(frame.message_type & 0xFF));
        ciphertext.insert(ciphertext.end(), frame.payload.begin(), frame.payload.end());

        crypto_->decrypt(ciphertext.data(), ciphertext.size(),
            [self, channel_id](const std::error_code& ec,
                               std::vector<uint8_t> plaintext) {
                if (ec) {
                    AA_LOG_E("decrypt failed: %s", ec.message().c_str());
                    self->handle_error(make_error_code(AapErrc::DecryptionFailed));
                    return;
                }
                if (plaintext.size() < 2) {
                    self->handle_error(make_error_code(AapErrc::FramingError));
                    return;
                }
                uint16_t msg_type = (static_cast<uint16_t>(plaintext[0]) << 8)
                                  | static_cast<uint16_t>(plaintext[1]);
                std::vector<uint8_t> payload(plaintext.begin() + 2,
                                             plaintext.end());
                self->dispatch_decrypted(channel_id, msg_type,
                                         std::move(payload));
            });
    } else {
        dispatch_decrypted(frame.channel_id, frame.message_type,
                           std::move(frame.payload));
    }
}

void Session::dispatch_decrypted(uint8_t channel_id, uint16_t msg_type,
                                 std::vector<uint8_t> payload) {
    AA_LOG_D("rx [%s] %s (%zu bytes) state=%s",
             channel_name(channel_id), msg_type_name(msg_type),
             payload.size(), to_string(state_));

    // During handshake, Session handles VERSION and SSL messages directly.
    // After handshake (Running), ALL messages go to registered services.
    if (state_ == SessionState::VersionExchange ||
        state_ == SessionState::SslHandshake) {
        auto ct = static_cast<ControlMessageType>(msg_type);
        switch (ct) {
            case ControlMessageType::VersionResponse:
                on_version_response(payload);
                return;
            case ControlMessageType::EncapsulatedSsl:
                on_ssl_data_received(payload.data(), payload.size());
                return;
            default:
                AA_LOG_W("unexpected %s during handshake", msg_type_name(msg_type));
                return;
        }
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

void Session::handle_error(const std::error_code& ec) {
    if (is_terminal(state_)) return;

    AA_LOG_E("session %u error: %s", config_.session_id, ec.message().c_str());
    transition_to(SessionState::Error);

    state_timer_.cancel();
    transport_->close();

    for (auto& [ch_id, svc] : services_) {
        svc->on_channel_close();
    }

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
            transition_to(SessionState::Disconnected);
            transport_->close();
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

void Session::send_version_request() {
    // VERSION_REQUEST payload is raw bytes, not protobuf:
    // [major:2 BE][minor:2 BE]
    // VERSION_REQUEST payload: [major:2 BE][minor:2 BE]
    // Use v1.1 — matches reference headunit. Phone responds with its version.
    std::vector<uint8_t> payload = {0, 1, 0, 1};
    send_message(kControlChannelId,
        static_cast<uint16_t>(ControlMessageType::VersionRequest),
        payload);
    AA_LOG_D("sent VERSION_REQUEST (v1.1)");
}

void Session::on_version_response(const std::vector<uint8_t>& payload) {
    // VERSION_RESPONSE is raw bytes: [major:2 BE][minor:2 BE][status:2 BE]
    if (payload.size() >= 6) {
        uint16_t major  = (payload[0] << 8) | payload[1];
        uint16_t minor  = (payload[2] << 8) | payload[3];
        int16_t  status = static_cast<int16_t>((payload[4] << 8) | payload[5]);
        AA_LOG_I("VERSION_RESPONSE: v%u.%u status=%d", major, minor, status);
        if (status != 0) {
            AA_LOG_E("phone refused version (status=%d)", status);
            handle_error(make_error_code(AapErrc::VersionMismatch));
            return;
        }
    } else {
        AA_LOG_W("VERSION_RESPONSE short (%zu bytes), assuming OK", payload.size());
    }

    // Version OK → start SSL handshake
    AA_LOG_I("version exchange complete, starting SSL handshake");
    begin_ssl_handshake();
}

void Session::begin_ssl_handshake() {
    transition_to(SessionState::SslHandshake);
    start_state_timer(config_.ssl_handshake_timeout_ms);

    auto self = shared_from_this();
    crypto_->handshake_step(nullptr, 0,
        [self](const std::error_code& ec, crypto::HandshakeResult result) {
            if (ec) {
                self->handle_error(make_error_code(AapErrc::SslHandshakeFailed));
                return;
            }
            if (!result.output_bytes.empty()) {
                self->send_message(kControlChannelId,
                    static_cast<uint16_t>(ControlMessageType::EncapsulatedSsl),
                    result.output_bytes);
            }
            if (result.complete) {
                self->on_ssl_complete();
            }
        });
}

void Session::on_ssl_data_received(const uint8_t* data, std::size_t size) {
    if (state_ != SessionState::SslHandshake) return;

    auto self = shared_from_this();
    crypto_->handshake_step(data, size,
        [self](const std::error_code& ec, crypto::HandshakeResult result) {
            if (ec) {
                self->handle_error(make_error_code(AapErrc::SslHandshakeFailed));
                return;
            }
            if (!result.output_bytes.empty()) {
                self->send_message(kControlChannelId,
                    static_cast<uint16_t>(ControlMessageType::EncapsulatedSsl),
                    result.output_bytes);
            }
            if (result.complete) {
                self->on_ssl_complete();
            }
        });
}

void Session::on_ssl_complete() {
    AA_LOG_I("SSL handshake complete, sending AUTH_COMPLETE (plaintext)");

    // AUTH_COMPLETE must be sent UNENCRYPTED — it's the last plaintext message.
    // After this message, all subsequent messages will be encrypted.
    pb_ctrl::AuthResponse auth;
    auth.set_status(0);
    auto payload = serialize(auth);

    // Build and send as plaintext frame directly
    std::vector<uint8_t> full_payload;
    uint16_t msg_type = static_cast<uint16_t>(ControlMessageType::AuthComplete);
    full_payload.push_back(static_cast<uint8_t>((msg_type >> 8) & 0xFF));
    full_payload.push_back(static_cast<uint8_t>(msg_type & 0xFF));
    full_payload.insert(full_payload.end(), payload.begin(), payload.end());

    auto flags = compute_frame_flags(kControlChannelId, msg_type, false);
    OutboundFrame frame{kControlChannelId, flags, std::move(full_payload)};
    auto wire_frames = framer_.encode(frame);
    for (auto& wire : wire_frames) {
        enqueue_write(std::move(wire));
    }

    // Handshake complete — transition to Running.
    // ControlService will handle ServiceDiscovery, ChannelOpen, Ping, etc.
    transition_to(SessionState::Running);
    AA_LOG_I("session running, all messages delegated to services");
}

} // namespace aauto::session
