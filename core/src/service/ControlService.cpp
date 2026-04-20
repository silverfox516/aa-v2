#define LOG_TAG "AA.ControlService"

#include "aauto/service/ControlService.hpp"
#include "aauto/engine/Engine.hpp"
#include "aauto/utils/Logger.hpp"
#include "aauto/utils/ProtocolUtil.hpp"

#include <aap_protobuf/service/control/message/ServiceDiscoveryRequest.pb.h>
#include <aap_protobuf/shared/PhoneInfo.pb.h>
#include <aap_protobuf/service/control/message/ServiceDiscoveryResponse.pb.h>
#include <aap_protobuf/service/control/message/HeadUnitInfo.pb.h>
#include <aap_protobuf/service/control/message/DriverPosition.pb.h>
#include <aap_protobuf/service/control/message/ConnectionConfiguration.pb.h>
#include <aap_protobuf/service/control/message/PingConfiguration.pb.h>
#include <aap_protobuf/service/control/message/PingRequest.pb.h>
#include <aap_protobuf/service/control/message/PingResponse.pb.h>
#include <aap_protobuf/service/control/message/AudioFocusNotification.pb.h>
#include <aap_protobuf/service/control/message/AudioFocusStateType.pb.h>
#include <aap_protobuf/service/control/message/NavFocusNotification.pb.h>
#include <aap_protobuf/service/control/message/NavFocusType.pb.h>
#include <aap_protobuf/service/control/message/ByeByeRequest.pb.h>
#include <aap_protobuf/service/control/message/ByeByeReason.pb.h>
#include <aap_protobuf/service/control/message/ByeByeResponse.pb.h>
#include <aap_protobuf/service/control/message/AudioFocusRequest.pb.h>
#include <aap_protobuf/service/control/message/AuthResponse.pb.h>
#include <aap_protobuf/service/Service.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>

#include <chrono>

namespace aauto::service {

namespace pb_ctrl = aap_protobuf::service::control::message;

static std::vector<uint8_t> serialize(const google::protobuf::MessageLite& msg) {
    std::vector<uint8_t> buf(msg.ByteSize());
    msg.SerializeToArray(buf.data(), static_cast<int>(buf.size()));
    return buf;
}

static int64_t steady_now_ns() {
    return std::chrono::steady_clock::now().time_since_epoch().count();
}

ControlService::ControlService(
        SendMessageFn send_fn,
        const engine::HeadunitConfig& hu_config,
        std::map<int32_t, std::shared_ptr<IService>> peer_services)
    : ServiceBase(std::move(send_fn))
    , hu_config_(hu_config)
    , peer_services_(std::move(peer_services)) {

    using CT = ControlMessageType;

    // SERVICE_DISCOVERY_REQUEST
    register_handler(static_cast<uint16_t>(CT::ServiceDiscoveryRequest),
        [this](const uint8_t* data, std::size_t size) {
            pb_ctrl::ServiceDiscoveryRequest req;
            if (req.ParseFromArray(data, static_cast<int>(size))) {
                std::string phone_name = req.has_device_name()
                    ? req.device_name() : "unknown";
                AA_LOG_I("phone connected: %s (%s)", phone_name.c_str(),
                         req.has_label_text() ? req.label_text().c_str() : "");
                if (req.has_phone_info()) {
                    const auto& pi = req.phone_info();
                    if (pi.has_instance_id()) {
                        AA_LOG_I("  instance_id: %s", pi.instance_id().c_str());
                    }
                }
                // Append phone name to session tag for all subsequent logs.
                // Strip manufacturer prefix (e.g., "samsung SM-N981N" -> "SM-N981N")
                auto pos = phone_name.find(' ');
                std::string short_name = (pos != std::string::npos)
                    ? phone_name.substr(pos + 1) : phone_name;
                session_tag_ += ":" + short_name;
                aauto::set_session_tag(session_tag_);
            }
            send_service_discovery_response();
        });

    // AUDIO_FOCUS_REQUEST
    register_handler(static_cast<uint16_t>(CT::AudioFocusRequest),
        [this](const uint8_t* data, std::size_t size) {
            pb_ctrl::AudioFocusRequest req;
            if (!req.ParseFromArray(data, static_cast<int>(size))) {
                AA_LOG_W("failed to parse AudioFocusRequest");
                return;
            }
            auto request_type = req.audio_focus_type();
            pb_ctrl::AudioFocusStateType state;
            switch (request_type) {
                case pb_ctrl::AUDIO_FOCUS_GAIN:
                    state = pb_ctrl::AUDIO_FOCUS_STATE_GAIN;
                    break;
                case pb_ctrl::AUDIO_FOCUS_GAIN_TRANSIENT:
                    state = pb_ctrl::AUDIO_FOCUS_STATE_GAIN_TRANSIENT;
                    break;
                case pb_ctrl::AUDIO_FOCUS_GAIN_TRANSIENT_MAY_DUCK:
                    state = pb_ctrl::AUDIO_FOCUS_STATE_GAIN_TRANSIENT_GUIDANCE_ONLY;
                    break;
                case pb_ctrl::AUDIO_FOCUS_RELEASE:
                    state = pb_ctrl::AUDIO_FOCUS_STATE_LOSS;
                    break;
                default:
                    state = pb_ctrl::AUDIO_FOCUS_STATE_GAIN;
                    break;
            }
            static auto focus_req_name = [](int t) -> const char* {
                switch (t) {
                    case 1: return "GAIN";
                    case 2: return "GAIN_TRANSIENT";
                    case 3: return "GAIN_TRANSIENT_MAY_DUCK";
                    case 4: return "RELEASE";
                    default: return "UNKNOWN";
                }
            };
            static auto focus_state_name = [](int s) -> const char* {
                switch (s) {
                    case 1: return "GAIN";
                    case 2: return "GAIN_TRANSIENT";
                    case 3: return "LOSS";
                    case 5: return "LOSS_TRANSIENT";
                    case 7: return "GAIN_TRANSIENT_GUIDANCE_ONLY";
                    default: return "UNKNOWN";
                }
            };
            AA_LOG_I("audio focus: %s -> %s",
                     focus_req_name(request_type), focus_state_name(state));
            pb_ctrl::AudioFocusNotification notif;
            notif.set_focus_state(state);
            notif.set_unsolicited(false);
            send(static_cast<uint16_t>(CT::AudioFocusNotification),
                 serialize(notif));
        });

    // NAV_FOCUS_REQUEST
    register_handler(static_cast<uint16_t>(CT::NavFocusRequest),
        [this](const uint8_t*, std::size_t) {
            AA_LOG_I("nav focus request, granting PROJECTED");
            pb_ctrl::NavFocusNotification notif;
            notif.set_focus_type(pb_ctrl::NAV_FOCUS_PROJECTED);
            send(static_cast<uint16_t>(CT::NavFocusNotification),
                 serialize(notif));
        });

    // PING_REQUEST — echo back
    register_handler(static_cast<uint16_t>(CT::PingRequest),
        [this](const uint8_t* data, std::size_t size) {
            pb_ctrl::PingRequest req;
            if (req.ParseFromArray(data, static_cast<int>(size))) {
                pb_ctrl::PingResponse resp;
                resp.set_timestamp(req.timestamp());
                send(static_cast<uint16_t>(CT::PingResponse), serialize(resp));
            }
        });

    // PING_RESPONSE — feed heartbeat watchdog
    register_handler(static_cast<uint16_t>(CT::PingResponse),
        [this](const uint8_t*, std::size_t) {
            last_pong_ns_ = steady_now_ns();
        });

    // BYEBYE_REQUEST
    register_handler(static_cast<uint16_t>(CT::ByeByeRequest),
        [this](const uint8_t*, std::size_t) {
            AA_LOG_I("received ByeByeRequest");
            send(static_cast<uint16_t>(CT::ByeByeResponse), {});
            trigger_session_close("ByeByeRequest");
        });

    // BYEBYE_RESPONSE
    register_handler(static_cast<uint16_t>(CT::ByeByeResponse),
        [this](const uint8_t*, std::size_t) {
            AA_LOG_I("received ByeByeResponse");
            trigger_session_close("ByeByeResponse");
        });

    // VOICE_SESSION / BATTERY — log only
    register_handler(static_cast<uint16_t>(CT::VoiceSessionNotification),
        [](const uint8_t*, std::size_t) {
            AA_LOG_D("voice session notification");
        });
    register_handler(static_cast<uint16_t>(CT::BatteryStatusNotification),
        [](const uint8_t*, std::size_t) {
            AA_LOG_D("battery status notification");
        });
}

ControlService::~ControlService() {
    on_channel_close();
}

void ControlService::on_channel_open(uint8_t channel_id) {
    ServiceBase::on_channel_open(channel_id);
    AA_LOG_I("control channel opened, starting heartbeat");
    last_pong_ns_ = steady_now_ns();
    close_triggered_ = false;
    running_ = true;
    heartbeat_thread_ = std::thread([this] {
        aauto::set_session_tag(session_tag_);
        heartbeat_loop();
    });
}

void ControlService::on_channel_close() {
    running_ = false;
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    ServiceBase::on_channel_close();
}

void ControlService::send_service_discovery_response() {
    pb_ctrl::ServiceDiscoveryResponse resp;
    resp.set_display_name(hu_config_.display_name);
    resp.set_driver_position(pb_ctrl::DRIVER_POSITION_LEFT);
    resp.set_session_configuration(0);

    auto* hui = resp.mutable_headunit_info();
    hui->set_make(hu_config_.hu_make);
    hui->set_model(hu_config_.hu_model);
    hui->set_head_unit_make(hu_config_.hu_make);
    hui->set_head_unit_model(hu_config_.hu_model);
    hui->set_head_unit_software_version(hu_config_.hu_sw_ver);
    hui->set_head_unit_software_build(hu_config_.hu_sw_ver);

    auto* conn_cfg = resp.mutable_connection_configuration();
    auto* ping_cfg = conn_cfg->mutable_ping_configuration();
    ping_cfg->set_tracked_ping_count(5);
    ping_cfg->set_timeout_ms(3000);
    ping_cfg->set_interval_ms(1000);
    ping_cfg->set_high_latency_threshold_ms(200);

    // Each peer service fills its own config with its assigned service_id
    for (auto& [service_id, svc] : peer_services_) {
        auto* ch = resp.add_channels();
        ch->set_id(service_id);
        svc->fill_config(ch);
    }

    send(static_cast<uint16_t>(ControlMessageType::ServiceDiscoveryResponse),
         serialize(resp));
    AA_LOG_I("sent ServiceDiscoveryResponse (%d channels)", resp.channels_size());
}

void ControlService::send_ping() {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    pb_ctrl::PingRequest ping;
    ping.set_timestamp(ms);
    send(static_cast<uint16_t>(ControlMessageType::PingRequest), serialize(ping));
}

void ControlService::heartbeat_loop() {
    AA_LOG_D("heartbeat thread started");

    while (running_) {
        // Sleep in short intervals for responsive shutdown
        for (int i = 0; i < kPingIntervalMs / 100 && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!running_) break;

        send_ping();

        // Check for timeout
        auto elapsed_ns = steady_now_ns() - last_pong_ns_.load();
        auto elapsed_ms = elapsed_ns / 1000000;
        if (elapsed_ms > kPingTimeoutMs) {
            AA_LOG_E("ping timeout (%lld ms)", static_cast<long long>(elapsed_ms));
            trigger_session_close("PingTimeout");
            break;
        }
    }

    AA_LOG_D("heartbeat thread exited");
}

void ControlService::on_session_stop() {
    initiate_bye();
}

void ControlService::initiate_bye() {
    AA_LOG_I("initiating ByeBye");
    pb_ctrl::ByeByeRequest bye;
    bye.set_reason(pb_ctrl::USER_SELECTION);
    send(static_cast<uint16_t>(ControlMessageType::ByeByeRequest), serialize(bye));
}

void ControlService::trigger_session_close(const char* reason) {
    bool expected = false;
    if (!close_triggered_.compare_exchange_strong(expected, true)) {
        return;
    }
    AA_LOG_I("session close triggered: %s", reason);
    if (session_close_cb_) {
        session_close_cb_();
    }
}

} // namespace aauto::service
