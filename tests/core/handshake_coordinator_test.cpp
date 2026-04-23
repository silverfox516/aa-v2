#include <gtest/gtest.h>

#include "aauto/session/HandshakeCoordinator.hpp"
#include "aauto/utils/ProtocolConstants.hpp"

#include <deque>
#include <system_error>
#include <vector>

using namespace aauto;
using namespace aauto::session;

namespace {

class ScriptedHandshakeCrypto : public crypto::ICryptoStrategy {
public:
    struct Step {
        std::error_code ec;
        crypto::HandshakeResult result;
    };

    void push_step(Step step) {
        steps_.push_back(std::move(step));
    }

    void handshake_step(const uint8_t* input_data, std::size_t input_size,
                        crypto::HandshakeStepHandler handler) override {
        inputs_.emplace_back(input_data, input_data + input_size);
        if (steps_.empty()) {
            handler({}, crypto::HandshakeResult{{}, false});
            return;
        }
        auto step = std::move(steps_.front());
        steps_.pop_front();
        handler(step.ec, std::move(step.result));
    }

    void encrypt(const uint8_t*, std::size_t, crypto::CryptoHandler handler) override {
        handler({}, {});
    }

    void decrypt(const uint8_t*, std::size_t, crypto::CryptoHandler handler) override {
        handler({}, {});
    }

    bool is_established() const override { return false; }
    void reset() override {}

    const std::vector<std::vector<uint8_t>>& inputs() const { return inputs_; }

private:
    std::deque<Step> steps_;
    std::vector<std::vector<uint8_t>> inputs_;
};

struct SentControlMessage {
    uint16_t type;
    std::vector<uint8_t> payload;
};

} // namespace

TEST(HandshakeCoordinatorTest, VersionRefusedReportsVersionMismatch) {
    auto crypto = std::make_shared<ScriptedHandshakeCrypto>();
    std::error_code error;

    HandshakeCoordinator coordinator(
        crypto,
        [](uint16_t, const std::vector<uint8_t>&) {},
        [] {},
        [&error](const std::error_code& ec) { error = ec; });

    coordinator.on_version_response({0x00, 0x01, 0x00, 0x07, 0xFF, 0xFF});

    EXPECT_EQ(error, make_error_code(AapErrc::VersionMismatch));
}

TEST(HandshakeCoordinatorTest, BeginSslHandshakeSendsEncapsulatedSslOutput) {
    auto crypto = std::make_shared<ScriptedHandshakeCrypto>();
    crypto->push_step({{}, {{0xAA, 0xBB}, false}});
    std::vector<SentControlMessage> sent;
    int completion_count = 0;

    HandshakeCoordinator coordinator(
        crypto,
        [&sent](uint16_t type, const std::vector<uint8_t>& payload) {
            sent.push_back({type, payload});
        },
        [&completion_count] { ++completion_count; },
        [](const std::error_code&) {});

    coordinator.begin_ssl_handshake();

    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].type,
              static_cast<uint16_t>(ControlMessageType::EncapsulatedSsl));
    EXPECT_EQ(sent[0].payload, std::vector<uint8_t>({0xAA, 0xBB}));
    EXPECT_EQ(completion_count, 0);
    ASSERT_EQ(crypto->inputs().size(), 1u);
    EXPECT_TRUE(crypto->inputs()[0].empty());
}

TEST(HandshakeCoordinatorTest, SslInputCompleteTriggersCompletion) {
    auto crypto = std::make_shared<ScriptedHandshakeCrypto>();
    crypto->push_step({{}, {{0x10}, true}});
    std::vector<SentControlMessage> sent;
    int completion_count = 0;

    HandshakeCoordinator coordinator(
        crypto,
        [&sent](uint16_t type, const std::vector<uint8_t>& payload) {
            sent.push_back({type, payload});
        },
        [&completion_count] { ++completion_count; },
        [](const std::error_code&) {});

    const std::vector<uint8_t> input = {0x01, 0x02, 0x03};
    coordinator.on_ssl_data_received(input.data(), input.size());

    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].type,
              static_cast<uint16_t>(ControlMessageType::EncapsulatedSsl));
    EXPECT_EQ(completion_count, 1);
    ASSERT_EQ(crypto->inputs().size(), 1u);
    EXPECT_EQ(crypto->inputs()[0], input);
}

TEST(HandshakeCoordinatorTest, HandshakeStepErrorReportsSslFailure) {
    auto crypto = std::make_shared<ScriptedHandshakeCrypto>();
    crypto->push_step({std::make_error_code(std::errc::connection_aborted), {{}, false}});
    std::error_code error;

    HandshakeCoordinator coordinator(
        crypto,
        [](uint16_t, const std::vector<uint8_t>&) {},
        [] {},
        [&error](const std::error_code& ec) { error = ec; });

    coordinator.begin_ssl_handshake();

    EXPECT_EQ(error, make_error_code(AapErrc::SslHandshakeFailed));
}
