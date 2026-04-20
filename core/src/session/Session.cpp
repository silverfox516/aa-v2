#define LOG_TAG "AA.Session"

#include "aauto/session/Session.hpp"
#include "aauto/engine/Engine.hpp"
#include "aauto/utils/Logger.hpp"

#include <aap_protobuf/service/control/message/VersionRequestOptions.pb.h>
#include <aap_protobuf/service/control/message/VersionResponseOptions.pb.h>
#include <aap_protobuf/service/control/message/AuthResponse.pb.h>
#include <aap_protobuf/service/control/message/ServiceDiscoveryRequest.pb.h>
#include <aap_protobuf/service/control/message/ServiceDiscoveryResponse.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenRequest.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>
#include <aap_protobuf/service/control/message/HeadUnitInfo.pb.h>
#include <aap_protobuf/service/control/message/PingRequest.pb.h>
#include <aap_protobuf/service/control/message/PingResponse.pb.h>
#include <aap_protobuf/service/control/message/ByeByeRequest.pb.h>
#include <aap_protobuf/service/control/message/ByeByeReason.pb.h>
#include <aap_protobuf/service/Service.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>

#include <chrono>

namespace aauto::session {

namespace pb_ctrl = aap_protobuf::service::control::message;
namespace pb_ver  = aap_protobuf::channel::control::version;
namespace pb_svc  = aap_protobuf::service;
namespace pb_shared = aap_protobuf::shared;

// Helper: serialize protobuf to byte vector
static std::vector<uint8_t> serialize(const google::protobuf::MessageLite& msg) {
    std::vector<uint8_t> buf(msg.ByteSize());
    msg.SerializeToArray(buf.data(), static_cast<int>(buf.size()));
    return buf;
}

Session::Session(asio::any_io_executor executor,
                 SessionConfig config,
                 const engine::HeadunitConfig& hu_config,
                 std::shared_ptr<transport::ITransport> transport,
                 std::shared_ptr<crypto::ICryptoStrategy> crypto,
                 ISessionObserver* observer)
    : strand_(asio::make_strand(executor))
    , config_(config)
    , hu_config_(hu_config)
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
            self->transition_to(SessionState::Disconnecting);
            self->start_state_timer(self->config_.byebye_timeout_ms);

            pb_ctrl::ByeByeRequest bye;
            bye.set_reason(pb_ctrl::USER_SELECTION);
            self->send_message(kControlChannelId,
                static_cast<uint16_t>(ControlMessageType::ByeByeRequest),
                serialize(bye));
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
    // VersionRequestOptions is optional; empty message is valid for protocol 1.6
    pb_ver::VersionRequestOptions req;
    send_message(kControlChannelId,
        static_cast<uint16_t>(ControlMessageType::VersionRequest),
        serialize(req));
    AA_LOG_D("sent VERSION_REQUEST (protocol %u.%u)",
             kProtocolVersionMajor, kProtocolVersionMinor);
}

void Session::on_version_response(const std::vector<uint8_t>& payload) {
    pb_ctrl::VersionResponseOptions resp;
    if (!resp.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        AA_LOG_W("failed to parse VERSION_RESPONSE, continuing anyway");
    }

    // Extract ping configuration if provided
    if (resp.has_connection_configuration() &&
        resp.connection_configuration().has_ping_configuration()) {
        const auto& ping = resp.connection_configuration().ping_configuration();
        if (ping.has_interval_ms() && ping.interval_ms() > 0) {
            config_.ping_interval_ms = ping.interval_ms();
            AA_LOG_D("ping interval updated: %u ms", config_.ping_interval_ms);
        }
        if (ping.has_timeout_ms() && ping.timeout_ms() > 0) {
            config_.ping_timeout_ms = ping.timeout_ms();
            AA_LOG_D("ping timeout updated: %u ms", config_.ping_timeout_ms);
        }
    }

    AA_LOG_I("received VERSION_RESPONSE, waiting for AUTH_COMPLETE");
    // Stay in VersionExchange state, wait for AUTH_COMPLETE from device
}

void Session::on_auth_complete(const std::vector<uint8_t>& payload) {
    pb_ctrl::AuthResponse auth;
    if (!auth.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        AA_LOG_E("failed to parse AUTH_COMPLETE");
        handle_error(make_error_code(AapErrc::AuthFailed));
        return;
    }

    if (auth.status() != static_cast<int32_t>(pb_shared::STATUS_SUCCESS)) {
        AA_LOG_E("AUTH_COMPLETE failed with status %d", auth.status());
        handle_error(make_error_code(AapErrc::AuthFailed));
        return;
    }

    AA_LOG_I("AUTH_COMPLETE success");
    transition_to(SessionState::ServiceDiscovery);
    start_state_timer(config_.service_discovery_timeout_ms);
    send_service_discovery_request();
}

void Session::send_service_discovery_request() {
    pb_ctrl::ServiceDiscoveryRequest req;
    req.set_device_name(hu_config_.display_name);

    send_message(kControlChannelId,
        static_cast<uint16_t>(ControlMessageType::ServiceDiscoveryRequest),
        serialize(req));
    AA_LOG_D("sent SERVICE_DISCOVERY_REQUEST (device=%s)",
             hu_config_.display_name.c_str());
}

void Session::on_service_discovery_response(const std::vector<uint8_t>& payload) {
    pb_ctrl::ServiceDiscoveryResponse resp;
    if (!resp.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        AA_LOG_E("failed to parse SERVICE_DISCOVERY_RESPONSE");
        handle_error(make_error_code(AapErrc::ServiceDiscoveryFailed));
        return;
    }

    AA_LOG_I("SERVICE_DISCOVERY_RESPONSE: %d channels", resp.channels_size());

    // Store discovered services for channel opening
    discovered_services_.clear();
    for (const auto& ch : resp.channels()) {
        AA_LOG_D("  service id=%d", ch.id());
        discovered_services_.push_back({ch.id(), 0});
    }

    if (discovered_services_.empty()) {
        AA_LOG_W("no services discovered from phone");
        handle_error(make_error_code(AapErrc::ServiceDiscoveryFailed));
        return;
    }

    transition_to(SessionState::ChannelSetup);
    start_state_timer(config_.channel_setup_timeout_ms);
    open_channels();
}

void Session::open_channels() {
    pending_channel_opens_ = 0;

    for (auto& ds : discovered_services_) {
        uint8_t ch = next_channel_id_++;
        ds.assigned_channel = ch;
        service_id_to_channel_[ds.service_id] = ch;

        pb_ctrl::ChannelOpenRequest req;
        req.set_priority(0);
        req.set_service_id(ds.service_id);

        send_message(kControlChannelId,
            static_cast<uint16_t>(ControlMessageType::ChannelOpenRequest),
            serialize(req));
        pending_channel_opens_++;

        AA_LOG_D("sent CHANNEL_OPEN_REQUEST: service_id=%d -> channel=%u",
                 ds.service_id, ch);
    }

    if (pending_channel_opens_ == 0) {
        transition_to(SessionState::Running);
        start_ping_timer();
    }
}

void Session::on_channel_open_response(const std::vector<uint8_t>& payload) {
    pb_ctrl::ChannelOpenResponse resp;
    if (!resp.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        AA_LOG_W("failed to parse CHANNEL_OPEN_RESPONSE");
    } else {
        auto status = resp.status();
        if (status != pb_shared::STATUS_SUCCESS) {
            AA_LOG_W("CHANNEL_OPEN_RESPONSE status=%d", static_cast<int>(status));
        }
    }

    pending_channel_opens_--;
    AA_LOG_D("CHANNEL_OPEN_RESPONSE received, pending=%d", pending_channel_opens_);

    if (pending_channel_opens_ <= 0
        && state_ == SessionState::ChannelSetup) {
        AA_LOG_I("all channels opened, transitioning to Running");
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

    auto now = std::chrono::steady_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    pb_ctrl::PingRequest ping;
    ping.set_timestamp(ms);

    send_message(kControlChannelId,
        static_cast<uint16_t>(ControlMessageType::PingRequest),
        serialize(ping));

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
            // Respond with same payload (echo timestamp)
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
