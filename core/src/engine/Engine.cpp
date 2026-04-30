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
        if (active_session_id_ == session_id) return;

        // Detach sinks from the previously active session so its decoded
        // frames stop reaching platform sinks (audio/video). Required to
        // avoid cross-stream output when two sessions are alive at once
        // (e.g., USB + Wireless on different phones).
        if (active_session_id_ != 0) {
            auto prev = sessions_.find(active_session_id_);
            if (prev != sessions_.end()) {
                prev->second->detach_all_sinks();
                AA_LOG_I("detached sinks: session=%u", active_session_id_);
            }
        }

        active_session_id_ = session_id;

        // Attach sinks to the new active session so its frames start
        // flowing to platform sinks. The opposite of the detach above —
        // both must be paired in this single transition.
        if (session_id != 0) {
            auto next = sessions_.find(session_id);
            if (next != sessions_.end()) {
                next->second->attach_all_sinks();
                AA_LOG_I("attached sinks: session=%u", session_id);
            }
        }
    });
}

void Engine::set_video_surface(uint32_t session_id, void* native_window) {
    AA_LOG_I("set_video_surface: session=%u window=%p", session_id, native_window);
    asio::post(io_context_, [this, session_id, native_window] {
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            it->second->set_video_surface(native_window);
        }
    });
}

void Engine::send_touch_event(uint32_t session_id,
                              int32_t x, int32_t y, int32_t action) {
    asio::post(io_context_, [this, session_id, x, y, action] {
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            it->second->send_touch_event(x, y, action);
        }
    });
}

void Engine::send_media_key(uint32_t session_id, int32_t keycode) {
    AA_LOG_I("send_media_key: session=%u keycode=%d", session_id, keycode);
    asio::post(io_context_, [this, session_id, keycode] {
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            it->second->send_media_key(keycode);
        }
    });
}

void Engine::release_audio_focus(uint32_t session_id) {
    AA_LOG_I("release_audio_focus: session=%u", session_id);
    asio::post(io_context_, [this, session_id] {
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            it->second->release_audio_focus();
        }
    });
}

void Engine::gain_audio_focus(uint32_t session_id) {
    AA_LOG_I("gain_audio_focus: session=%u", session_id);
    asio::post(io_context_, [this, session_id] {
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            it->second->gain_audio_focus();
        }
    });
}

void Engine::complete_pairing(uint32_t session_id, int32_t status,
                              bool already_paired) {
    AA_LOG_I("complete_pairing: session=%u status=%d already_paired=%d",
             session_id, status, already_paired ? 1 : 0);
    asio::post(io_context_, [this, session_id, status, already_paired] {
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            it->second->complete_pairing(status, already_paired);
        }
    });
}

void Engine::complete_auth(uint32_t session_id, int32_t status) {
    AA_LOG_I("complete_auth: session=%u status=%d", session_id, status);
    asio::post(io_context_, [this, session_id, status] {
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            it->second->complete_auth(status);
        }
    });
}

void Engine::set_video_focus(uint32_t session_id, bool projected) {
    AA_LOG_I("set_video_focus: session=%u projected=%d", session_id, projected);
    asio::post(io_context_, [this, session_id, projected] {
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            it->second->set_video_focus(projected);
        }
    });
}

void Engine::attach_all_sinks(uint32_t session_id) {
    asio::post(io_context_, [this, session_id] {
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            it->second->attach_all_sinks();
        }
    });
}

void Engine::detach_all_sinks(uint32_t session_id) {
    asio::post(io_context_, [this, session_id] {
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            it->second->detach_all_sinks();
        }
    });
}

// ===== Lifecycle =====

void Engine::run(unsigned int thread_count) {
    if (thread_count == 0) {
        thread_count = 1;
    }

    AA_LOG_I("engine run with %u threads", thread_count);
    threads_.reserve(thread_count > 0 ? thread_count - 1 : 0);
    for (unsigned int i = 1; i < thread_count; ++i) {
        threads_.emplace_back([this] { io_context_.run(); });
    }
    io_context_.run();

    for (auto& t : threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    threads_.clear();
}

void Engine::shutdown() {
    AA_LOG_I("engine shutdown");
    work_guard_.reset();
    io_context_.stop();
}

// ===== ISessionObserver =====

void Engine::on_session_state_changed(uint32_t session_id,
                                      session::SessionState state) {
    AA_LOG_I("session %u state: %s", session_id, session::to_string(state));

    if (session::is_terminal(state)) {
        cleanup_session(session_id);
    }

    if (callback_) {
        // Initialized to silence GCC false-positive maybe-uninitialized;
        // the switch below is exhaustive over SessionState.
        SessionStatus status = SessionStatus::Disconnected;
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

service::SendMessageFn Engine::make_send_fn(
        const std::shared_ptr<session::Session>& session) const {
    return [weak = std::weak_ptr<session::Session>(session)](
            uint8_t ch, uint16_t type, const std::vector<uint8_t>& payload) {
        if (auto s = weak.lock()) {
            s->send_message(ch, type, payload);
        }
    };
}

std::shared_ptr<session::Session> Engine::create_session(
        uint32_t sid,
        std::shared_ptr<transport::ITransport> transport) {
    auto crypto = crypto_factory_->create(config_.crypto_config);

    session::SessionConfig sconfig;
    sconfig.session_id = sid;

    return std::make_shared<session::Session>(
        io_context_.get_executor(), sconfig,
        std::move(transport), std::move(crypto), this);
}

std::shared_ptr<service::ControlService> Engine::create_control_service(
        uint32_t sid,
        const std::shared_ptr<session::Session>& session,
        const std::map<int32_t, std::shared_ptr<service::IService>>& peer_services) {
    auto control_svc = std::make_shared<service::ControlService>(
        io_context_.get_executor(), make_send_fn(session), config_, peer_services);
    control_svc->set_channel(kControlChannelId);
    control_svc->set_log_tag("s" + std::to_string(sid));
    control_svc->set_session_close_callback(
        [weak = std::weak_ptr<session::Session>(session)] {
            if (auto s = weak.lock()) {
                s->stop();
            }
        });
    control_svc->set_phone_identified_callback(
        [this, sid, weak = std::weak_ptr<session::Session>(session)]
        (const std::string& device_name) {
            // Update session log tag with phone name
            auto pos = device_name.find(' ');
            std::string short_name = (pos != std::string::npos)
                ? device_name.substr(pos + 1) : device_name;
            if (auto s = weak.lock()) {
                s->update_log_tag(short_name);
            }
            AA_LOG_I("phone identified: session=%u name=%s",
                     sid, device_name.c_str());
            if (callback_) {
                callback_->on_phone_identified(sid, device_name, "");
            }
        });
    control_svc->on_channel_open(kControlChannelId);  // implicit channel — start heartbeat
    return control_svc;
}

void Engine::register_services(
        const std::shared_ptr<session::Session>& session,
        std::map<int32_t, std::shared_ptr<service::IService>> peer_services,
        const std::shared_ptr<service::ControlService>& control_service) {
    session->register_service(kControlChannelId, control_service);

    for (auto& [service_id, svc] : peer_services) {
        session->register_service(static_cast<uint8_t>(service_id), std::move(svc));
    }
}

void Engine::report_start_session_failure(uint32_t sid, const std::string& detail) {
    if (!callback_) return;

    callback_->on_session_error(sid,
        make_error_code(AapErrc::TransportClosed),
        detail);
}

void Engine::do_start_session(const std::string& descriptor, uint32_t sid) {
    set_session_tag("s" + std::to_string(sid));
    auto transport = transport_factory_->create(
        io_context_.get_executor(), descriptor);
    if (!transport) {
        AA_LOG_E("failed to create transport for: %s", descriptor.c_str());
        report_start_session_failure(sid, "failed to create transport");
        return;
    }
    auto session = create_session(sid, std::move(transport));
    auto send_fn = make_send_fn(session);

    // Create peer services (ch 1~6: video, audio, input, sensor, etc.)
    service_factory_->set_session_id(sid);
    auto peer_services = service_factory_->create_services(send_fn);

    // Assign channel IDs to peer services before creating ControlService
    // (ControlService iterates peer_services to build ServiceDiscoveryResponse)
    for (auto& [service_id, svc] : peer_services) {
        svc->set_channel(static_cast<uint8_t>(service_id));
    }

    // Create ControlService (ch 0) with references to peer services
    auto control_svc = create_control_service(sid, session, peer_services);
    register_services(session, std::move(peer_services), control_svc);

    sessions_[sid] = session;
    session->start();
    // Note: no auto-promotion to active. The app must explicitly call
    // set_active_session() to make this session the sink-attached one.
    // See F.17 in docs/architecture_review.md.
}

void Engine::cleanup_session(uint32_t session_id) {
    sessions_.erase(session_id);
    if (active_session_id_ == session_id) {
        // Don't auto-promote another session. Sink attach state was
        // bound to the removed session; selecting the next active is a
        // policy decision for the app. See F.17.
        active_session_id_ = 0;
    }
}

} // namespace aauto::engine
