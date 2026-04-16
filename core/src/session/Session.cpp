#define LOG_TAG "Session"

#include "aauto/session/Session.hpp"
#include "aauto/utils/Logger.hpp"

#include <chrono>

namespace aauto::session {

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
    , state_timer_(strand_)
    , ping_timer_(strand_)
    , ping_timeout_timer_(strand_) {
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
    asio::post(strand_, [self] { self->begin_ssl_handshake(); });
}

void Session::stop() {
    auto self = shared_from_this();
    asio::post(strand_, [self] {
        if (is_terminal(self->state_)) return;

        if (self->state_ == SessionState::Running) {
            // Send ByeBye request
            self->transition_to(SessionState::Disconnecting);
            self->start_state_timer(self->config_.byebye_timeout_ms);

            // Build ByeByeRequest: reason = USER_SELECTION(1)
            std::vector<uint8_t> payload = {0x08, 0x01};  // field 1, varint 1
            self->send_message(kControlChannelId,
                static_cast<uint16_t>(ControlMessageType::ByeByeRequest),
                payload);
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

    bool needs_encryption = crypto_->is_established()
                         && channel_id != kControlChannelId;

    if (needs_encryption) {
        auto self = shared_from_this();
        crypto_->encrypt(full_payload.data(), full_payload.size(),
            [self, channel_id](const std::error_code& ec,
                               std::vector<uint8_t> ciphertext) {
                if (ec) {
                    AA_LOG_E("encrypt failed: %s", ec.message().c_str());
                    self->handle_error(ec);
                    return;
                }
                OutboundFrame frame{channel_id, true, std::move(ciphertext)};
                auto wire_frames = self->framer_.encode(frame);
                for (auto& wire : wire_frames) {
                    self->enqueue_write(std::move(wire));
                }
            });
    } else {
        OutboundFrame frame{channel_id, false, std::move(full_payload)};
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
    // During SSL handshake, all data on channel 0 is handshake data
    if (state_ == SessionState::SslHandshake
        && frame.channel_id == kControlChannelId) {
        on_ssl_data_received(frame.payload.data(), frame.payload.size());
        return;
    }

    if (frame.encrypted && crypto_->is_established()) {
        auto self = shared_from_this();
        auto channel_id = frame.channel_id;
        crypto_->decrypt(frame.payload.data(), frame.payload.size(),
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
    if (channel_id == kControlChannelId) {
        handle_control_message(msg_type, payload);
        return;
    }

    auto it = services_.find(channel_id);
    if (it != services_.end()) {
        it->second->on_message(msg_type, payload.data(), payload.size());
    } else {
        AA_LOG_W("message on unknown channel %u, type %u", channel_id, msg_type);
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
    ping_timer_.cancel();
    ping_timeout_timer_.cancel();
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
        case SessionState::SslHandshake:
            handle_error(make_error_code(AapErrc::SslHandshakeFailed));
            break;
        case SessionState::VersionExchange:
            handle_error(make_error_code(AapErrc::VersionMismatch));
            break;
        case SessionState::ServiceDiscovery:
            handle_error(make_error_code(AapErrc::ServiceDiscoveryFailed));
            break;
        case SessionState::ChannelSetup:
            handle_error(make_error_code(AapErrc::ChannelOpenFailed));
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

void Session::begin_ssl_handshake() {
    transition_to(SessionState::SslHandshake);
    start_state_timer(config_.ssl_handshake_timeout_ms);
    start_read();

    // Initiate SSL handshake (HU sends first)
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
    AA_LOG_I("SSL handshake complete");
    transition_to(SessionState::VersionExchange);
    start_state_timer(config_.version_exchange_timeout_ms);
    send_version_request();
}

void Session::send_version_request() {
    // VERSION_REQUEST: protocol version 1.6
    // Minimal protobuf encoding for VersionRequestOptions
    // (may need to be empty or contain snapshot_version)
    std::vector<uint8_t> payload;
    send_message(kControlChannelId,
        static_cast<uint16_t>(ControlMessageType::VersionRequest),
        payload);
    AA_LOG_D("sent VERSION_REQUEST");
}

void Session::on_version_response(const std::vector<uint8_t>& payload) {
    // TODO: parse VersionResponseOptions, extract ConnectionConfiguration
    (void)payload;
    AA_LOG_I("received VERSION_RESPONSE");
    // Wait for AUTH_COMPLETE from device
}

void Session::on_auth_complete(const std::vector<uint8_t>& payload) {
    (void)payload;
    AA_LOG_I("received AUTH_COMPLETE");
    transition_to(SessionState::ServiceDiscovery);
    start_state_timer(config_.service_discovery_timeout_ms);
    send_service_discovery_request();
}

void Session::send_service_discovery_request() {
    // TODO: build ServiceDiscoveryRequest with device_name, icons
    std::vector<uint8_t> payload;
    send_message(kControlChannelId,
        static_cast<uint16_t>(ControlMessageType::ServiceDiscoveryRequest),
        payload);
    AA_LOG_D("sent SERVICE_DISCOVERY_REQUEST");
}

void Session::on_service_discovery_response(const std::vector<uint8_t>& payload) {
    // TODO: parse ServiceDiscoveryResponse to enumerate available services
    (void)payload;
    AA_LOG_I("received SERVICE_DISCOVERY_RESPONSE");
    transition_to(SessionState::ChannelSetup);
    start_state_timer(config_.channel_setup_timeout_ms);
    open_channels();
}

void Session::open_channels() {
    // TODO: for each discovered service, send CHANNEL_OPEN_REQUEST
    // For now, this is a placeholder that transitions directly to Running.
    // In full implementation:
    //   for each service_config in discovery_response:
    //     send ChannelOpenRequest(service_id, priority)
    //     pending_channel_opens_++
    //
    // When all ChannelOpenResponses received -> transition to Running

    pending_channel_opens_ = 0;

    if (pending_channel_opens_ == 0) {
        transition_to(SessionState::Running);
        start_ping_timer();
    }
}

void Session::on_channel_open_response(const std::vector<uint8_t>& payload) {
    // TODO: parse response, check MessageStatus, register service
    (void)payload;
    pending_channel_opens_--;

    if (pending_channel_opens_ <= 0
        && state_ == SessionState::ChannelSetup) {
        transition_to(SessionState::Running);
        start_ping_timer();
    }
}

// ===== Ping =====

void Session::start_ping_timer() {
    ping_timer_.expires_after(
        std::chrono::milliseconds(config_.ping_interval_ms));
    auto self = shared_from_this();
    ping_timer_.async_wait([self](const std::error_code& ec) {
        if (!ec) {
            self->send_ping();
        }
    });
}

void Session::send_ping() {
    if (state_ != SessionState::Running) return;

    // PingRequest: field 1 = timestamp (int64)
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    // Simple protobuf: field 1 (wire type 0 = varint), value = ms
    // For simplicity, send timestamp as raw bytes in payload
    std::vector<uint8_t> payload;
    // protobuf field 1, wire type 0: tag = 0x08
    payload.push_back(0x08);
    // Encode varint
    auto val = static_cast<uint64_t>(ms);
    while (val > 0x7F) {
        payload.push_back(static_cast<uint8_t>((val & 0x7F) | 0x80));
        val >>= 7;
    }
    payload.push_back(static_cast<uint8_t>(val));

    send_message(kControlChannelId,
        static_cast<uint16_t>(ControlMessageType::PingRequest),
        payload);

    // Start ping timeout
    ping_timeout_timer_.expires_after(
        std::chrono::milliseconds(config_.ping_timeout_ms));
    auto self = shared_from_this();
    ping_timeout_timer_.async_wait([self](const std::error_code& ec) {
        if (!ec) {
            self->on_ping_timeout();
        }
    });
}

void Session::on_ping_response(const std::vector<uint8_t>& /*payload*/) {
    ping_timeout_timer_.cancel();
    start_ping_timer();
}

void Session::on_ping_timeout() {
    AA_LOG_E("ping timeout");
    handle_error(make_error_code(AapErrc::PingTimeout));
}

// ===== Control message dispatch =====

void Session::handle_control_message(uint16_t msg_type,
                                     const std::vector<uint8_t>& payload) {
    auto ct = static_cast<ControlMessageType>(msg_type);

    switch (ct) {
        case ControlMessageType::VersionResponse:
            on_version_response(payload);
            break;
        case ControlMessageType::AuthComplete:
            on_auth_complete(payload);
            break;
        case ControlMessageType::ServiceDiscoveryResponse:
            on_service_discovery_response(payload);
            break;
        case ControlMessageType::ChannelOpenResponse:
            on_channel_open_response(payload);
            break;
        case ControlMessageType::PingRequest: {
            // Respond with same timestamp
            send_message(kControlChannelId,
                static_cast<uint16_t>(ControlMessageType::PingResponse),
                payload);
            break;
        }
        case ControlMessageType::PingResponse:
            on_ping_response(payload);
            break;
        case ControlMessageType::ByeByeRequest: {
            AA_LOG_I("received ByeByeRequest");
            send_message(kControlChannelId,
                static_cast<uint16_t>(ControlMessageType::ByeByeResponse),
                {});
            transition_to(SessionState::Disconnecting);
            start_state_timer(config_.byebye_timeout_ms);
            break;
        }
        case ControlMessageType::ByeByeResponse:
            AA_LOG_I("received ByeByeResponse");
            transition_to(SessionState::Disconnected);
            transport_->close();
            break;
        case ControlMessageType::AudioFocusRequest:
            // TODO: handle audio focus
            AA_LOG_D("audio focus request received");
            break;
        case ControlMessageType::NavFocusRequest:
            AA_LOG_D("nav focus request received");
            break;
        case ControlMessageType::BatteryStatusNotification:
            AA_LOG_D("battery status received");
            break;
        default:
            AA_LOG_D("unhandled control message type %u", msg_type);
            break;
    }
}

} // namespace aauto::session
