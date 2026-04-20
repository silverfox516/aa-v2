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
#include <aap_protobuf/service/media/sink/MediaSinkService.pb.h>
#include <aap_protobuf/service/media/shared/message/MediaCodecType.pb.h>
#include <aap_protobuf/service/media/sink/message/VideoCodecResolutionType.pb.h>
#include <aap_protobuf/service/media/sink/message/VideoFrameRateType.pb.h>
#include <aap_protobuf/service/media/sink/message/VideoConfiguration.pb.h>
#include <aap_protobuf/service/media/sink/message/AudioStreamType.pb.h>
#include <aap_protobuf/service/media/shared/message/AudioConfiguration.pb.h>
#include <aap_protobuf/service/inputsource/InputSourceService.pb.h>
#include <aap_protobuf/service/inputsource/message/TouchScreenType.pb.h>
#include <aap_protobuf/service/sensorsource/SensorSourceService.pb.h>
#include <aap_protobuf/service/sensorsource/message/Sensor.pb.h>
#include <aap_protobuf/service/sensorsource/message/SensorType.pb.h>
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
    asio::post(strand_, [self] { self->begin_version_exchange(); });
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

    // After SSL handshake, ALL channels are encrypted (including control)
    bool needs_encryption = crypto_->is_established();

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
            // Timeout in ChannelSetup = no more channel opens coming
            // If we received at least one, transition to Running
            if (!service_id_to_channel_.empty()) {
                AA_LOG_I("channel setup complete, transitioning to Running");
                transition_to(SessionState::Running);
                start_ping_timer();
            } else {
                handle_error(make_error_code(AapErrc::ChannelOpenFailed));
            }
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
// AAP protocol order: VERSION → SSL → AUTH → SERVICE_DISCOVERY → CHANNEL_OPEN

void Session::begin_version_exchange() {
    transition_to(SessionState::VersionExchange);
    start_state_timer(config_.version_exchange_timeout_ms);
    start_read();
    send_version_request();
}

void Session::send_version_request() {
    // VERSION_REQUEST payload is raw bytes, not protobuf:
    // [major:2 BE][minor:2 BE]
    std::vector<uint8_t> payload = {0, 1, 0, 1};  // AAP v1.1
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

void Session::on_auth_complete(const std::vector<uint8_t>& /*payload*/) {
    // Not expected from phone in this protocol flow.
    // HU sends AUTH_COMPLETE after SSL, not the phone.
    AA_LOG_W("unexpected AUTH_COMPLETE from phone, ignoring");
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
    // Temporarily mark crypto as not established to bypass encryption.
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

    OutboundFrame frame{kControlChannelId, false, std::move(full_payload)};
    auto wire_frames = framer_.encode(frame);
    for (auto& wire : wire_frames) {
        enqueue_write(std::move(wire));
    }

    // Wait for phone's ServiceDiscoveryRequest
    transition_to(SessionState::ServiceDiscovery);
    start_state_timer(config_.service_discovery_timeout_ms);
    AA_LOG_I("waiting for ServiceDiscoveryRequest from phone");
}

void Session::on_service_discovery_request(const std::vector<uint8_t>& payload) {
    pb_ctrl::ServiceDiscoveryRequest req;
    if (req.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        if (req.has_device_name()) {
            AA_LOG_I("phone device: %s", req.device_name().c_str());
        }
    }
    AA_LOG_I("received ServiceDiscoveryRequest, sending response");
    send_service_discovery_response();
}

void Session::send_service_discovery_response() {
    namespace pb_media = aap_protobuf::service::media::shared::message;
    namespace pb_video = aap_protobuf::service::media::sink::message;
    namespace pb_input = aap_protobuf::service::inputsource;
    namespace pb_sensor = aap_protobuf::service::sensorsource::message;

    pb_ctrl::ServiceDiscoveryResponse resp;
    resp.set_display_name(hu_config_.display_name);
    resp.set_driver_position(pb_ctrl::DRIVER_POSITION_LEFT);
    resp.set_session_configuration(0);

    // HeadUnitInfo — all fields populated
    auto* hui = resp.mutable_headunit_info();
    hui->set_make(hu_config_.hu_make);
    hui->set_model(hu_config_.hu_model);
    hui->set_head_unit_make(hu_config_.hu_make);
    hui->set_head_unit_model(hu_config_.hu_model);
    hui->set_head_unit_software_version(hu_config_.hu_sw_ver);
    hui->set_head_unit_software_build(hu_config_.hu_sw_ver);

    // Connection/ping configuration
    auto* conn_cfg = resp.mutable_connection_configuration();
    auto* ping_cfg = conn_cfg->mutable_ping_configuration();
    ping_cfg->set_tracked_ping_count(5);
    ping_cfg->set_timeout_ms(3000);
    ping_cfg->set_interval_ms(1000);
    ping_cfg->set_high_latency_threshold_ms(200);

    // Channel 1: Video sink
    auto* video_ch = resp.add_channels();
    video_ch->set_id(1);
    auto* video_sink = video_ch->mutable_media_sink_service();
    video_sink->set_available_type(pb_media::MEDIA_CODEC_VIDEO_H264_BP);
    auto* video_cfg = video_sink->add_video_configs();
    video_cfg->set_codec_resolution(pb_video::VIDEO_800x480);
    video_cfg->set_frame_rate(pb_video::VIDEO_FPS_30);
    video_cfg->set_density(hu_config_.video_density);
    video_cfg->set_width_margin(0);
    video_cfg->set_height_margin(0);

    // Channel 2: Audio sink (media)
    auto* audio_media_ch = resp.add_channels();
    audio_media_ch->set_id(2);
    auto* audio_media = audio_media_ch->mutable_media_sink_service();
    audio_media->set_available_type(pb_media::MEDIA_CODEC_AUDIO_PCM);
    audio_media->set_audio_type(pb_video::AUDIO_STREAM_MEDIA);
    auto* audio_media_cfg = audio_media->add_audio_configs();
    audio_media_cfg->set_sampling_rate(hu_config_.audio_sample_rate);
    audio_media_cfg->set_number_of_bits(hu_config_.audio_bit_depth);
    audio_media_cfg->set_number_of_channels(hu_config_.audio_channels);

    // Channel 3: Audio sink (guidance — navigation voice)
    auto* audio_guide_ch = resp.add_channels();
    audio_guide_ch->set_id(3);
    auto* audio_guide = audio_guide_ch->mutable_media_sink_service();
    audio_guide->set_available_type(pb_media::MEDIA_CODEC_AUDIO_PCM);
    audio_guide->set_audio_type(pb_video::AUDIO_STREAM_GUIDANCE);
    auto* audio_guide_cfg = audio_guide->add_audio_configs();
    audio_guide_cfg->set_sampling_rate(16000);
    audio_guide_cfg->set_number_of_bits(16);
    audio_guide_cfg->set_number_of_channels(1);

    // Channel 4: Audio sink (system)
    auto* audio_sys_ch = resp.add_channels();
    audio_sys_ch->set_id(4);
    auto* audio_sys = audio_sys_ch->mutable_media_sink_service();
    audio_sys->set_available_type(pb_media::MEDIA_CODEC_AUDIO_PCM);
    audio_sys->set_audio_type(pb_video::AUDIO_STREAM_SYSTEM_AUDIO);
    auto* audio_sys_cfg = audio_sys->add_audio_configs();
    audio_sys_cfg->set_sampling_rate(16000);
    audio_sys_cfg->set_number_of_bits(16);
    audio_sys_cfg->set_number_of_channels(1);

    // Channel 5: Input source (touchscreen)
    auto* input_ch = resp.add_channels();
    input_ch->set_id(5);
    auto* input_svc = input_ch->mutable_input_source_service();
    auto* ts = input_svc->add_touchscreen();
    ts->set_width(hu_config_.video_width);
    ts->set_height(hu_config_.video_height);
    ts->set_type(pb_input::message::CAPACITIVE);

    // Channel 6: Sensor source (driving status + night mode)
    auto* sensor_ch = resp.add_channels();
    sensor_ch->set_id(6);
    auto* sensor_svc = sensor_ch->mutable_sensor_source_service();
    auto* s1 = sensor_svc->add_sensors();
    s1->set_sensor_type(pb_sensor::SENSOR_DRIVING_STATUS_DATA);
    auto* s2 = sensor_svc->add_sensors();
    s2->set_sensor_type(pb_sensor::SENSOR_NIGHT_MODE);

    send_message(kControlChannelId,
        static_cast<uint16_t>(ControlMessageType::ServiceDiscoveryResponse),
        serialize(resp));
    AA_LOG_I("sent ServiceDiscoveryResponse (%d channels)", resp.channels_size());

    // Now wait for ChannelOpenRequests from phone
    transition_to(SessionState::ChannelSetup);
    start_state_timer(config_.channel_setup_timeout_ms);
}

void Session::on_channel_open_request(const std::vector<uint8_t>& payload) {
    pb_ctrl::ChannelOpenRequest req;
    if (!req.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        AA_LOG_W("failed to parse ChannelOpenRequest");
        return;
    }

    int32_t service_id = req.service_id();
    uint8_t ch = next_channel_id_++;
    service_id_to_channel_[service_id] = ch;

    AA_LOG_I("ChannelOpenRequest: service_id=%d -> channel=%u", service_id, ch);

    // Respond with success
    pb_ctrl::ChannelOpenResponse resp;
    resp.set_status(pb_shared::STATUS_SUCCESS);
    send_message(kControlChannelId,
        static_cast<uint16_t>(ControlMessageType::ChannelOpenResponse),
        serialize(resp));

    // Reset channel setup timer on each request (phone may send multiple)
    start_state_timer(config_.channel_setup_timeout_ms);
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
        case ControlMessageType::EncapsulatedSsl:
            on_ssl_data_received(payload.data(), payload.size());
            break;
        case ControlMessageType::VersionResponse:
            on_version_response(payload);
            break;
        case ControlMessageType::AuthComplete:
            on_auth_complete(payload);
            break;
        case ControlMessageType::ServiceDiscoveryRequest:
            on_service_discovery_request(payload);
            break;
        case ControlMessageType::ChannelOpenRequest:
            on_channel_open_request(payload);
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
