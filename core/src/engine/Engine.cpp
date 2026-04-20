#define LOG_TAG "AA.Engine"

#include "aauto/engine/Engine.hpp"
#include "aauto/service/ControlService.hpp"
#include "aauto/utils/Logger.hpp"

namespace aauto::engine {

Engine::Engine(HeadunitConfig config,
               std::shared_ptr<ITransportFactory> transport_factory,
               std::shared_ptr<ICryptoFactory> crypto_factory,
               std::shared_ptr<IServiceFactory> service_factory)
    : io_context_()
    , work_guard_(asio::make_work_guard(io_context_))
    , config_(std::move(config))
    , transport_factory_(std::move(transport_factory))
    , crypto_factory_(std::move(crypto_factory))
    , service_factory_(std::move(service_factory)) {
    AA_LOG_I("engine created: %s %s", config_.hu_make.c_str(),
             config_.hu_model.c_str());
}

Engine::~Engine() {
    shutdown();
}

// ===== IEngineController =====

void Engine::register_callback(IEngineCallback* cb) {
    callback_ = cb;
}

uint32_t Engine::start_session(const std::string& transport_descriptor) {
    uint32_t sid = next_session_id_.fetch_add(1);
    AA_LOG_I("start_session: id=%u, descriptor=%s", sid,
             transport_descriptor.c_str());

    asio::post(io_context_, [this, descriptor = transport_descriptor, sid] {
        do_start_session(descriptor, sid);
    });

    return sid;
}

void Engine::stop_session(uint32_t session_id) {
    AA_LOG_I("stop_session: id=%u", session_id);
    asio::post(io_context_, [this, session_id] {
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            it->second->stop();
        }
    });
}

void Engine::stop_all() {
    AA_LOG_I("stop_all");
    asio::post(io_context_, [this] {
        for (auto& [id, session] : sessions_) {
            session->stop();
        }
    });
}

void Engine::set_active_session(uint32_t session_id) {
    AA_LOG_I("set_active_session: id=%u", session_id);
    asio::post(io_context_, [this, session_id] {
        active_session_id_ = session_id;
        // TODO: detach sinks from old active, attach to new active
    });
}

// ===== Lifecycle =====

void Engine::run(unsigned int thread_count) {
    AA_LOG_I("engine run with %u threads", thread_count);
    threads_.reserve(thread_count);
    for (unsigned int i = 0; i < thread_count; ++i) {
        threads_.emplace_back([this] { io_context_.run(); });
    }
    for (auto& t : threads_) {
        t.join();
    }
}

void Engine::shutdown() {
    AA_LOG_I("engine shutdown");
    work_guard_.reset();

    for (auto& [id, session] : sessions_) {
        session->stop();
    }

    io_context_.stop();

    for (auto& t : threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    threads_.clear();
    sessions_.clear();
}

// ===== ISessionObserver =====

void Engine::on_session_state_changed(uint32_t session_id,
                                      session::SessionState state) {
    AA_LOG_I("session %u state: %s", session_id, session::to_string(state));

    if (session::is_terminal(state)) {
        cleanup_session(session_id);
    }

    if (callback_) {
        SessionStatus status;
        switch (state) {
            case session::SessionState::Idle:
                status = SessionStatus::Connecting;
                break;
            case session::SessionState::VersionExchange:
            case session::SessionState::SslHandshake:
                status = SessionStatus::Handshaking;
                break;
            case session::SessionState::Running:
                status = SessionStatus::Running;
                break;
            case session::SessionState::Disconnecting:
                status = SessionStatus::Disconnecting;
                break;
            case session::SessionState::Disconnected:
                status = SessionStatus::Disconnected;
                break;
            case session::SessionState::Error:
                status = SessionStatus::Error;
                break;
        }
        callback_->on_session_state_changed(session_id, status);
    }
}

void Engine::on_session_error(uint32_t session_id,
                              const std::error_code& ec) {
    AA_LOG_E("session %u error: %s", session_id, ec.message().c_str());

    if (callback_) {
        callback_->on_session_error(session_id, ec, ec.message());
    }
}

// ===== Internal =====

void Engine::do_start_session(const std::string& descriptor, uint32_t sid) {
    auto transport = transport_factory_->create(
        io_context_.get_executor(), descriptor);
    if (!transport) {
        AA_LOG_E("failed to create transport for: %s", descriptor.c_str());
        if (callback_) {
            callback_->on_session_error(sid,
                make_error_code(AapErrc::TransportClosed),
                "failed to create transport");
        }
        return;
    }

    auto crypto = crypto_factory_->create(config_.crypto_config);

    session::SessionConfig sconfig;
    sconfig.session_id = sid;

    auto session = std::make_shared<session::Session>(
        io_context_.get_executor(), sconfig,
        std::move(transport), std::move(crypto), this);

    // Create send function bound to this session
    auto send_fn = [weak = std::weak_ptr<session::Session>(session)](
            uint8_t ch, uint16_t type, const std::vector<uint8_t>& payload) {
        if (auto s = weak.lock()) {
            s->send_message(ch, type, payload);
        }
    };

    // Create peer services (ch 1~6: video, audio, input, sensor, etc.)
    auto peer_services = service_factory_->create_services(send_fn);

    // Assign channel IDs to peer services before creating ControlService
    // (ControlService iterates peer_services to build ServiceDiscoveryResponse)
    for (auto& [service_id, svc] : peer_services) {
        svc->set_channel(static_cast<uint8_t>(service_id));
    }

    // Create ControlService (ch 0) with references to peer services
    auto control_svc = std::make_shared<service::ControlService>(
        send_fn, config_, peer_services);
    control_svc->set_channel(kControlChannelId);
    control_svc->set_log_tag("s" + std::to_string(sid));
    control_svc->set_session_close_callback(
        [weak = std::weak_ptr<session::Session>(session)] {
            if (auto s = weak.lock()) {
                s->stop();
            }
        });
    session->register_service(kControlChannelId, control_svc);

    // Register peer services
    for (auto& [service_id, svc] : peer_services) {
        session->register_service(static_cast<uint8_t>(service_id), std::move(svc));
    }

    sessions_[sid] = session;

    if (active_session_id_ == 0) {
        active_session_id_ = sid;
    }

    session->start();
}

void Engine::cleanup_session(uint32_t session_id) {
    sessions_.erase(session_id);
    if (active_session_id_ == session_id) {
        active_session_id_ = sessions_.empty() ? 0 : sessions_.begin()->first;
    }
}

} // namespace aauto::engine
