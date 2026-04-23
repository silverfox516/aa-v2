#include <gtest/gtest.h>

#include "aauto/session/Session.hpp"
#include "aauto/session/Framer.hpp"
#include "aauto/utils/ProtocolConstants.hpp"

#include "mock/MockTransport.hpp"
#include "mock/MockCrypto.hpp"
#include "mock/MockSessionObserver.hpp"

#include <aap_protobuf/service/control/message/VersionResponseOptions.pb.h>
#include <aap_protobuf/service/control/message/AuthResponse.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>

#include <asio.hpp>
#include <chrono>
#include <vector>

using namespace aauto;
using namespace aauto::session;
using namespace aauto::test;

namespace pb_ctrl = aap_protobuf::service::control::message;
namespace pb_shared = aap_protobuf::shared;

// Helper: serialize protobuf to byte vector
static std::vector<uint8_t> serialize(const google::protobuf::MessageLite& msg) {
    std::vector<uint8_t> buf(msg.ByteSizeLong());
    msg.SerializeToArray(buf.data(), static_cast<int>(buf.size()));
    return buf;
}

// Helper: build AAP wire frame with message type prefix
static std::vector<uint8_t> make_control_frame(uint16_t msg_type,
                                                const std::vector<uint8_t>& body) {
    // payload = [msg_type:2 BE][body]
    std::vector<uint8_t> payload;
    payload.push_back(static_cast<uint8_t>((msg_type >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>(msg_type & 0xFF));
    payload.insert(payload.end(), body.begin(), body.end());

    // wire = [channel:1][flags:1][len:2 BE][payload]
    uint16_t len = static_cast<uint16_t>(payload.size());
    std::vector<uint8_t> wire;
    wire.push_back(kControlChannelId);  // channel 0
    wire.push_back(static_cast<uint8_t>(FragInfo::Unfragmented));
    wire.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    wire.push_back(static_cast<uint8_t>(len & 0xFF));
    wire.insert(wire.end(), payload.begin(), payload.end());
    return wire;
}

class SessionHandshakeTest : public ::testing::Test {
protected:
    void SetUp() override {
        transport_ = std::make_shared<MockTransport>(io_.get_executor());
        crypto_ = std::make_shared<MockCrypto>();

        SessionConfig sconfig;
        sconfig.session_id = 1;
        sconfig.ssl_handshake_timeout_ms = 1000;
        sconfig.version_exchange_timeout_ms = 1000;

        session_ = std::make_shared<Session>(
            io_.get_executor(), sconfig,
            transport_, crypto_, &observer_);
    }

    // Run io_context until no more work (with a safety limit)
    void run_io(int max_iterations = 100) {
        io_.restart();
        for (int i = 0; i < max_iterations; ++i) {
            if (io_.poll() == 0) break;
        }
    }

    // Feed a control message to the session via mock transport
    void feed_control_message(uint16_t msg_type,
                              const google::protobuf::MessageLite& msg) {
        auto wire = make_control_frame(msg_type, serialize(msg));
        transport_->feed_read(wire);
        run_io();
    }

    void feed_control_message(uint16_t msg_type,
                              const std::vector<uint8_t>& body = {}) {
        auto wire = make_control_frame(msg_type, body);
        transport_->feed_read(wire);
        run_io();
    }

    asio::io_context io_;
    std::shared_ptr<MockTransport> transport_;
    std::shared_ptr<MockCrypto> crypto_;
    MockSessionObserver observer_;
    std::shared_ptr<Session> session_;
};

// ===== Version Exchange =====

TEST_F(SessionHandshakeTest, StartTransitionsToVersionExchange) {
    session_->start();
    run_io();

    // MockCrypto completes handshake immediately, so we go to VersionExchange
    EXPECT_EQ(observer_.last_state(), SessionState::VersionExchange);
}

TEST_F(SessionHandshakeTest, VersionRequestIsSent) {
    session_->start();
    run_io();

    // Check that VERSION_REQUEST was written to transport
    const auto& written = transport_->get_written_data();
    EXPECT_FALSE(written.empty());
}

// ===== SSL Handshake =====

TEST_F(SessionHandshakeTest, VersionResponseStartsSslHandshake) {
    session_->start();
    run_io();
    transport_->clear_written_data();

    // Send VERSION_RESPONSE (raw bytes: major=1, minor=7, status=0)
    std::vector<uint8_t> ver_body = {0, 1, 0, 7, 0, 0};
    feed_control_message(
        static_cast<uint16_t>(ControlMessageType::VersionResponse), ver_body);

    // Session should transition to SslHandshake
    // (MockCrypto may auto-complete SSL, advancing further)
    bool saw_ssl = false;
    for (const auto& [id, state] : observer_.state_changes()) {
        if (state == SessionState::SslHandshake) saw_ssl = true;
    }
    EXPECT_TRUE(saw_ssl);
}

// ===== SSL Complete → Running =====

TEST_F(SessionHandshakeTest, SslCompleteTransitionsToRunning) {
    session_->start();
    run_io();

    // Send VERSION_RESPONSE to trigger SSL handshake
    std::vector<uint8_t> ver_body = {0, 1, 0, 7, 0, 0};
    feed_control_message(
        static_cast<uint16_t>(ControlMessageType::VersionResponse), ver_body);

    // MockCrypto auto-completes SSL → AUTH_COMPLETE sent → Running
    EXPECT_EQ(session_->state(), SessionState::Running);
}

TEST_F(SessionHandshakeTest, FullHandshakeStateSequence) {
    session_->start();
    run_io();

    std::vector<uint8_t> ver_body = {0, 1, 0, 7, 0, 0};
    feed_control_message(
        static_cast<uint16_t>(ControlMessageType::VersionResponse), ver_body);

    // Verify full state transition sequence
    std::vector<SessionState> expected_states = {
        SessionState::VersionExchange,
        SessionState::SslHandshake,
        SessionState::Running,
    };

    ASSERT_GE(observer_.state_changes().size(), expected_states.size());
    for (size_t i = 0; i < expected_states.size(); ++i) {
        EXPECT_EQ(observer_.state_changes()[i].second, expected_states[i])
            << "State mismatch at index " << i;
    }
}

// ===== Version Mismatch =====

TEST_F(SessionHandshakeTest, VersionResponseRefusedTransitionsToError) {
    session_->start();
    run_io();

    // Send VERSION_RESPONSE with non-zero status (refused)
    std::vector<uint8_t> ver_body = {0, 1, 0, 7, 0xFF, 0xFF};  // status = -1
    feed_control_message(
        static_cast<uint16_t>(ControlMessageType::VersionResponse), ver_body);

    EXPECT_EQ(session_->state(), SessionState::Error);
}

// ===== Post-handshake delegation =====

TEST_F(SessionHandshakeTest, PostHandshakeMessagesGoToServices) {
    // Drive to Running
    session_->start();
    run_io();
    std::vector<uint8_t> ver_body = {0, 1, 0, 7, 0, 0};
    feed_control_message(
        static_cast<uint16_t>(ControlMessageType::VersionResponse), ver_body);
    ASSERT_EQ(session_->state(), SessionState::Running);

    // Register a mock service on channel 0
    // (In real usage, ControlService would be here)
    // Without a service registered, messages to ch 0 should log a warning
    // but not crash or change state
    feed_control_message(
        static_cast<uint16_t>(ControlMessageType::PingRequest));

    // Session stays in Running (message was delegated, not handled internally)
    EXPECT_EQ(session_->state(), SessionState::Running);
}

// ===== Lifecycle =====

class SessionLifecycleTest : public SessionHandshakeTest {
protected:
    void drive_to_running() {
        session_->start();
        run_io();

        std::vector<uint8_t> ver_body = {0, 1, 0, 7, 0, 0};
        feed_control_message(
            static_cast<uint16_t>(ControlMessageType::VersionResponse), ver_body);

        ASSERT_EQ(session_->state(), SessionState::Running);
        transport_->clear_written_data();
    }
};

// ===== Error: transport closed during Running =====

TEST_F(SessionLifecycleTest, TransportErrorInRunningTransitionsToError) {
    drive_to_running();

    // Simulate USB disconnect
    transport_->inject_read_error(asio::error::connection_reset);
    run_io();

    EXPECT_EQ(session_->state(), SessionState::Error);
    EXPECT_FALSE(observer_.errors().empty());
}

// ===== Error: stop from non-Running state =====

TEST_F(SessionLifecycleTest, StopDuringHandshakeTransitionsToError) {
    session_->start();
    run_io();

    ASSERT_EQ(session_->state(), SessionState::VersionExchange);

    session_->stop();
    run_io();

    EXPECT_EQ(session_->state(), SessionState::Error);
}

// ===== Stop without ControlService falls back to Disconnected =====

TEST_F(SessionLifecycleTest, StopWithoutControlServiceDisconnects) {
    drive_to_running();

    // No ControlService registered on ch 0 → fallback path
    session_->stop();
    run_io();

    EXPECT_EQ(session_->state(), SessionState::Disconnected);
    EXPECT_FALSE(transport_->is_open());
}
