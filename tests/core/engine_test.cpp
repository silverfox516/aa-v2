#include <gtest/gtest.h>

#include "aauto/engine/Engine.hpp"

#include "mock/MockCrypto.hpp"
#include "mock/MockTransport.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace aauto;
using namespace aauto::engine;
using namespace aauto::test;

namespace {

class NoopService : public service::IService {
public:
    void set_channel(uint8_t channel_id) override { channel_id_ = channel_id; }
    uint8_t channel_id() const override { return channel_id_; }
    void on_channel_open(uint8_t channel_id) override { channel_id_ = channel_id; }
    void on_message(uint16_t, const uint8_t*, std::size_t) override {}
    void on_channel_close() override {}
    service::ServiceType type() const override { return service::ServiceType::MediaSink; }
    void fill_config(aap_protobuf::service::ServiceConfiguration*) override {}

private:
    uint8_t channel_id_ = 0;
};

class FakeTransportFactory : public ITransportFactory {
public:
    std::shared_ptr<transport::ITransport>
    create(asio::any_io_executor executor, const std::string& descriptor) override {
        descriptors.push_back(descriptor);
        if (!should_create_transport) {
            return nullptr;
        }
        last_transport = std::make_shared<MockTransport>(executor);
        return last_transport;
    }

    bool should_create_transport = false;
    std::shared_ptr<MockTransport> last_transport;
    std::vector<std::string> descriptors;
};

class FakeCryptoFactory : public ICryptoFactory {
public:
    std::shared_ptr<crypto::ICryptoStrategy>
    create(const crypto::CryptoConfig&) override {
        ++create_count;
        return std::make_shared<MockCrypto>();
    }

    int create_count = 0;
};

class FakeServiceFactory : public IServiceFactory {
public:
    void set_session_id(uint32_t id) override { last_session_id = id; }

    std::map<int32_t, std::shared_ptr<service::IService>>
    create_services(service::SendMessageFn) override {
        ++create_count;
        return {{1, std::make_shared<NoopService>()}};
    }

    int create_count = 0;
    uint32_t last_session_id = 0;
};

// Records attach_sinks / detach_sinks calls so tests can observe Engine's
// active-session sink swap behavior.
class RecordingService : public service::IService {
public:
    void set_channel(uint8_t channel_id) override { channel_id_ = channel_id; }
    uint8_t channel_id() const override { return channel_id_; }
    void on_channel_open(uint8_t channel_id) override { channel_id_ = channel_id; }
    void on_message(uint16_t, const uint8_t*, std::size_t) override {}
    void on_channel_close() override {}
    service::ServiceType type() const override { return service::ServiceType::MediaSink; }
    void fill_config(aap_protobuf::service::ServiceConfiguration*) override {}

    void attach_sinks() override { ++attach_count; }
    void detach_sinks() override { ++detach_count; }

    std::atomic<int> attach_count{0};
    std::atomic<int> detach_count{0};

private:
    uint8_t channel_id_ = 0;
};

// Service factory that hands out RecordingService instances and remembers
// which one belongs to which session, so tests can read attach/detach
// counts per session.
class TrackingServiceFactory : public IServiceFactory {
public:
    void set_session_id(uint32_t id) override { current_id_ = id; }

    std::map<int32_t, std::shared_ptr<service::IService>>
    create_services(service::SendMessageFn) override {
        auto svc = std::make_shared<RecordingService>();
        services_by_session_[current_id_] = svc;
        return {{1, svc}};
    }

    std::shared_ptr<RecordingService> get(uint32_t session_id) const {
        auto it = services_by_session_.find(session_id);
        return it != services_by_session_.end() ? it->second : nullptr;
    }

private:
    uint32_t current_id_ = 0;
    std::map<uint32_t, std::shared_ptr<RecordingService>> services_by_session_;
};

// Generic poll-with-timeout helper for asynchronous tests where there is
// no explicit completion signal (set_active_session is fire-and-forget).
template <typename Pred>
bool wait_until(Pred pred,
                std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
}

class RecordingEngineCallback : public IEngineCallback {
public:
    void on_session_state_changed(uint32_t session_id, SessionStatus status) override {
        {
            std::lock_guard<std::mutex> lock(mu_);
            states_.push_back({session_id, status});
        }
        cv_.notify_all();
    }

    void on_session_error(uint32_t session_id,
                          const std::error_code& ec,
                          const std::string& detail) override {
        {
            std::lock_guard<std::mutex> lock(mu_);
            errors_.push_back({session_id, ec, detail});
        }
        cv_.notify_all();
    }

    void on_phone_identified(uint32_t, const std::string&, const std::string&) override {}
    void on_video_data(uint32_t, const uint8_t*, std::size_t, int64_t, bool) override {}
    void on_audio_data(uint32_t, uint32_t, const uint8_t*, std::size_t, int64_t) override {}
    void on_video_focus_changed(uint32_t, bool) override {}

    bool wait_for_state(SessionStatus wanted,
                        std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
        std::unique_lock<std::mutex> lock(mu_);
        return cv_.wait_for(lock, timeout, [&] {
            for (const auto& entry : states_) {
                if (entry.status == wanted) {
                    return true;
                }
            }
            return false;
        });
    }

    // Wait until at least `count` distinct session IDs have reported the
    // given state. Used to synchronize on multi-session start-up.
    bool wait_for_distinct_sessions(SessionStatus wanted, std::size_t count,
                                    std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
        std::unique_lock<std::mutex> lock(mu_);
        return cv_.wait_for(lock, timeout, [&] {
            std::set<uint32_t> seen;
            for (const auto& entry : states_) {
                if (entry.status == wanted) seen.insert(entry.session_id);
            }
            return seen.size() >= count;
        });
    }

    bool wait_for_error(std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
        std::unique_lock<std::mutex> lock(mu_);
        return cv_.wait_for(lock, timeout, [&] { return !errors_.empty(); });
    }

    struct StateEntry {
        uint32_t session_id;
        SessionStatus status;
    };

    struct ErrorEntry {
        uint32_t session_id;
        std::error_code ec;
        std::string detail;
    };

    std::vector<StateEntry> states() const {
        std::lock_guard<std::mutex> lock(mu_);
        return states_;
    }

    std::vector<ErrorEntry> errors() const {
        std::lock_guard<std::mutex> lock(mu_);
        return errors_;
    }

private:
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::vector<StateEntry> states_;
    std::vector<ErrorEntry> errors_;
};

} // namespace

TEST(EngineTest, StartSessionWithoutTransportReportsError) {
    auto transport_factory = std::make_shared<FakeTransportFactory>();
    auto crypto_factory = std::make_shared<FakeCryptoFactory>();
    auto service_factory = std::make_shared<FakeServiceFactory>();

    Engine engine(HeadunitConfig{},
                  transport_factory,
                  crypto_factory,
                  service_factory);

    RecordingEngineCallback callback;
    engine.register_callback(&callback);

    std::thread runner([&engine] { engine.run(); });

    const uint32_t session_id = engine.start_session("missing-transport");

    ASSERT_TRUE(callback.wait_for_error());
    const auto errors = callback.errors();
    ASSERT_FALSE(errors.empty());
    EXPECT_EQ(errors.front().session_id, session_id);
    EXPECT_EQ(errors.front().detail, "failed to create transport");
    EXPECT_EQ(transport_factory->descriptors,
              std::vector<std::string>{"missing-transport"});
    EXPECT_EQ(crypto_factory->create_count, 0);
    EXPECT_EQ(service_factory->create_count, 0);

    engine.shutdown();
    runner.join();
}

TEST(EngineTest, StartSessionCreatesTransportServicesAndEntersHandshaking) {
    auto transport_factory = std::make_shared<FakeTransportFactory>();
    transport_factory->should_create_transport = true;
    auto crypto_factory = std::make_shared<FakeCryptoFactory>();
    auto service_factory = std::make_shared<FakeServiceFactory>();

    Engine engine(HeadunitConfig{},
                  transport_factory,
                  crypto_factory,
                  service_factory);

    RecordingEngineCallback callback;
    engine.register_callback(&callback);

    std::thread runner([&engine] { engine.run(); });

    const uint32_t session_id = engine.start_session("usb:1");

    ASSERT_TRUE(callback.wait_for_state(SessionStatus::Handshaking));
    const auto states = callback.states();
    ASSERT_FALSE(states.empty());
    EXPECT_EQ(states.front().session_id, session_id);
    EXPECT_EQ(states.front().status, SessionStatus::Handshaking);
    EXPECT_EQ(transport_factory->descriptors, std::vector<std::string>{"usb:1"});
    EXPECT_EQ(crypto_factory->create_count, 1);
    EXPECT_EQ(service_factory->create_count, 1);

    engine.shutdown();
    runner.join();
}

// Verifies F.17: set_active_session is the single transactional entry
// point for sink swap across sessions. With two live sessions, switching
// the active one must (a) detach sinks from the previously active session
// and (b) attach them to the new active session. Re-activating the same
// session must be a no-op.
TEST(EngineTest, SetActiveSessionSwapsSinks) {
    auto transport_factory = std::make_shared<FakeTransportFactory>();
    transport_factory->should_create_transport = true;
    auto crypto_factory = std::make_shared<FakeCryptoFactory>();
    auto service_factory = std::make_shared<TrackingServiceFactory>();

    Engine engine(HeadunitConfig{},
                  transport_factory,
                  crypto_factory,
                  service_factory);

    RecordingEngineCallback callback;
    engine.register_callback(&callback);

    std::thread runner([&engine] { engine.run(); });

    const uint32_t sid1 = engine.start_session("usb:1");
    const uint32_t sid2 = engine.start_session("usb:2");

    ASSERT_TRUE(callback.wait_for_distinct_sessions(SessionStatus::Handshaking, 2));

    auto svc1 = service_factory->get(sid1);
    auto svc2 = service_factory->get(sid2);
    ASSERT_NE(svc1, nullptr);
    ASSERT_NE(svc2, nullptr);

    // No automatic attach on session start (F.17: app must explicitly
    // call set_active_session for sinks to engage).
    EXPECT_EQ(svc1->attach_count.load(), 0);
    EXPECT_EQ(svc1->detach_count.load(), 0);
    EXPECT_EQ(svc2->attach_count.load(), 0);
    EXPECT_EQ(svc2->detach_count.load(), 0);

    // Activate session 1: only svc1 attaches, svc2 untouched.
    engine.set_active_session(sid1);
    ASSERT_TRUE(wait_until([&] { return svc1->attach_count.load() == 1; }));
    EXPECT_EQ(svc1->attach_count.load(), 1);
    EXPECT_EQ(svc1->detach_count.load(), 0);
    EXPECT_EQ(svc2->attach_count.load(), 0);
    EXPECT_EQ(svc2->detach_count.load(), 0);

    // Switch active to session 2: svc1 detaches, svc2 attaches.
    engine.set_active_session(sid2);
    ASSERT_TRUE(wait_until([&] {
        return svc1->detach_count.load() == 1 && svc2->attach_count.load() == 1;
    }));
    EXPECT_EQ(svc1->attach_count.load(), 1);
    EXPECT_EQ(svc1->detach_count.load(), 1);
    EXPECT_EQ(svc2->attach_count.load(), 1);
    EXPECT_EQ(svc2->detach_count.load(), 0);

    // Re-activating the current active session is a no-op: counts stay put.
    engine.set_active_session(sid2);
    // Drain io_context by issuing a follow-up call we can wait on. There
    // is no public sync primitive; instead, switch back to sid1 and wait
    // for the resulting detach on svc2 — that confirms the prior no-op
    // call was processed and produced no extra attach/detach.
    engine.set_active_session(sid1);
    ASSERT_TRUE(wait_until([&] {
        return svc2->detach_count.load() == 1 && svc1->attach_count.load() == 2;
    }));
    EXPECT_EQ(svc1->attach_count.load(), 2);
    EXPECT_EQ(svc1->detach_count.load(), 1);
    EXPECT_EQ(svc2->attach_count.load(), 1);
    EXPECT_EQ(svc2->detach_count.load(), 1);

    engine.shutdown();
    runner.join();
}
