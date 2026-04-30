#pragma once

#include "aauto/engine/HeadunitConfig.hpp"
#include "aauto/engine/IEngineController.hpp"
#include "aauto/session/Session.hpp"
#include "aauto/crypto/ICryptoStrategy.hpp"
#include "aauto/transport/ITransport.hpp"

#include <asio.hpp>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace aauto::service { class ControlService; }

namespace aauto::engine {

/// Factory: creates platform-specific transports.
class ITransportFactory {
public:
    virtual ~ITransportFactory() = default;
    virtual std::shared_ptr<transport::ITransport>
        create(asio::any_io_executor executor,
               const std::string& descriptor) = 0;
};

/// Factory: creates crypto strategy with certs.
class ICryptoFactory {
public:
    virtual ~ICryptoFactory() = default;
    virtual std::shared_ptr<crypto::ICryptoStrategy>
        create(const crypto::CryptoConfig& config) = 0;
};

/// Factory: creates services for a session.
class IServiceFactory {
public:
    virtual ~IServiceFactory() = default;
    virtual void set_session_id(uint32_t id) = 0;
    virtual std::map<int32_t, std::shared_ptr<service::IService>>
        create_services(service::SendMessageFn send_fn) = 0;
};

/// Top-level engine. Owns io_context, manages sessions.
///
/// Multi-session: map<session_id, Session>.
/// One "active" session has sinks attached; others dormant.
///
/// Threading: io_context on configurable thread count (default 1).
/// Session logic is strand-protected.
class Engine : public IEngineController,
               public session::ISessionObserver {
public:
    Engine(HeadunitConfig config,
           std::shared_ptr<ITransportFactory> transport_factory,
           std::shared_ptr<ICryptoFactory> crypto_factory,
           std::shared_ptr<IServiceFactory> service_factory);
    ~Engine();

    // IEngineController
    void register_callback(IEngineCallback* cb) override;
    uint32_t start_session(const std::string& transport_descriptor) override;
    void stop_session(uint32_t session_id) override;
    void stop_all() override;
    void set_active_session(uint32_t session_id) override;
    void set_video_surface(uint32_t session_id, void* native_window) override;
    void set_video_focus(uint32_t session_id, bool projected) override;
    void attach_all_sinks(uint32_t session_id) override;
    void detach_all_sinks(uint32_t session_id) override;
    void send_touch_event(uint32_t session_id,
                          int32_t x, int32_t y, int32_t action) override;
    void send_media_key(uint32_t session_id, int32_t keycode) override;
    void release_audio_focus(uint32_t session_id) override;
    void gain_audio_focus(uint32_t session_id) override;
    void complete_pairing(uint32_t session_id, int32_t status,
                          bool already_paired) override;
    void complete_auth(uint32_t session_id, int32_t status) override;

    /// Run event loop. Blocks until shutdown().
    void run(unsigned int thread_count = 1);

    /// Stop all sessions and event loop.
    void shutdown();

    // ISessionObserver
    void on_session_state_changed(uint32_t session_id,
                                  session::SessionState state) override;
    void on_session_error(uint32_t session_id,
                          const std::error_code& ec) override;

private:
    service::SendMessageFn make_send_fn(
        const std::shared_ptr<session::Session>& session) const;
    std::shared_ptr<session::Session> create_session(
        uint32_t sid,
        std::shared_ptr<transport::ITransport> transport);
    std::shared_ptr<service::ControlService> create_control_service(
        uint32_t sid,
        const std::shared_ptr<session::Session>& session,
        const std::map<int32_t, std::shared_ptr<service::IService>>& peer_services);
    void register_services(
        const std::shared_ptr<session::Session>& session,
        std::map<int32_t, std::shared_ptr<service::IService>> peer_services,
        const std::shared_ptr<service::ControlService>& control_service);
    void report_start_session_failure(uint32_t sid, const std::string& detail);
    void do_start_session(const std::string& descriptor, uint32_t sid);
    void cleanup_session(uint32_t session_id);

    asio::io_context io_context_;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
    std::vector<std::thread> threads_;

    HeadunitConfig config_;
    std::shared_ptr<ITransportFactory> transport_factory_;
    std::shared_ptr<ICryptoFactory>    crypto_factory_;
    std::shared_ptr<IServiceFactory>   service_factory_;
    IEngineCallback*                   callback_ = nullptr;

    std::map<uint32_t, std::shared_ptr<session::Session>> sessions_;
    uint32_t active_session_id_ = 0;
    std::atomic<uint32_t> next_session_id_{1};
};

} // namespace aauto::engine
