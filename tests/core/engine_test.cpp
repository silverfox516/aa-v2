#include <gtest/gtest.h>

#include "aauto/engine/Engine.hpp"

#include "mock/MockCrypto.hpp"
#include "mock/MockTransport.hpp"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
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
    std::map<int32_t, std::shared_ptr<service::IService>>
    create_services(service::SendMessageFn) override {
        ++create_count;
        return {{1, std::make_shared<NoopService>()}};
    }

    int create_count = 0;
};

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
