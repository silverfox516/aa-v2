#pragma once

#include "aauto/service/ServiceBase.hpp"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>

namespace aauto::engine { struct HeadunitConfig; }

namespace aauto::service {

/// Control channel (channel 0) service.
///
/// Handles all AAP control messages after handshake:
///   - ServiceDiscovery (request from phone → response with HU capabilities)
///   - AudioFocus (request → grant)
///   - NavFocus (request → grant)
///   - Ping/Pong (echo + heartbeat timeout detection)
///   - ByeBye (graceful disconnect)
///
/// Does NOT handle handshake (VERSION, SSL, AUTH) — Session manages those.
class ControlService : public ServiceBase {
public:
    using SessionCloseCallback = std::function<void()>;

    ControlService(SendMessageFn send_fn,
                   const engine::HeadunitConfig& hu_config,
                   std::map<int32_t, std::shared_ptr<IService>> peer_services);
    ~ControlService();

    ServiceType type() const override { return ServiceType::Control; }
    void on_channel_open(uint8_t channel_id) override;
    void on_channel_close() override;

    void set_session_close_callback(SessionCloseCallback cb) {
        session_close_cb_ = std::move(cb);
    }

    void set_log_tag(std::string tag) { session_tag_ = std::move(tag); }

    void on_session_stop() override;

private:
    void initiate_bye();
    void send_service_discovery_response();
    void send_ping();
    void heartbeat_loop();
    void trigger_session_close(const char* reason);

    const engine::HeadunitConfig& hu_config_;
    std::map<int32_t, std::shared_ptr<IService>> peer_services_;
    SessionCloseCallback session_close_cb_;
    std::string session_tag_;

    // Heartbeat
    std::thread heartbeat_thread_;
    std::atomic<bool> running_{false};
    std::atomic<int64_t> last_pong_ns_{0};
    std::atomic<bool> close_triggered_{false};

    static constexpr int kPingIntervalMs = 5000;
    static constexpr int kPingTimeoutMs  = 10000;
};

} // namespace aauto::service
