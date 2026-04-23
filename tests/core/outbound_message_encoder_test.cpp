#include <gtest/gtest.h>

#include "aauto/session/Framer.hpp"
#include "aauto/session/OutboundMessageEncoder.hpp"
#include "aauto/utils/ProtocolConstants.hpp"

#include "mock/MockCrypto.hpp"

#include <system_error>
#include <vector>

using namespace aauto;
using namespace aauto::session;
using namespace aauto::test;

namespace {

class EstablishedCrypto : public MockCrypto {
public:
    EstablishedCrypto() {
        handshake_step(nullptr, 0, [](const std::error_code&, crypto::HandshakeResult) {});
    }
};

class FailingEncryptCrypto : public crypto::ICryptoStrategy {
public:
    void handshake_step(const uint8_t*, std::size_t,
                        crypto::HandshakeStepHandler handler) override {
        handler({}, crypto::HandshakeResult{{}, true});
    }

    void encrypt(const uint8_t*, std::size_t, crypto::CryptoHandler handler) override {
        handler(std::make_error_code(std::errc::io_error), {});
    }

    void decrypt(const uint8_t*, std::size_t, crypto::CryptoHandler handler) override {
        handler({}, {});
    }

    bool is_established() const override { return true; }
    void reset() override {}
};

std::vector<AapFragment> decode_frames(const std::vector<std::vector<uint8_t>>& wires) {
    Framer framer;
    std::vector<AapFragment> fragments;
    for (const auto& wire : wires) {
        framer.feed(wire.data(), wire.size(),
            [&fragments](AapFragment frag) {
                fragments.push_back(std::move(frag));
            });
    }
    return fragments;
}

} // namespace

TEST(OutboundMessageEncoderTest, PlaintextMessageEncodesTypePrefixedPayload) {
    auto crypto = std::make_shared<MockCrypto>();
    std::vector<std::vector<uint8_t>> wires;
    std::error_code error;

    OutboundMessageEncoder encoder(
        crypto,
        [&error](const std::error_code& ec) { error = ec; },
        [&wires](std::vector<uint8_t> wire) { wires.push_back(std::move(wire)); });

    encoder.send_message(kControlChannelId,
                         static_cast<uint16_t>(ControlMessageType::VersionRequest),
                         {0xAA, 0xBB});

    ASSERT_FALSE(error);
    auto fragments = decode_frames(wires);
    ASSERT_EQ(fragments.size(), 1u);
    EXPECT_EQ(fragments[0].channel_id, kControlChannelId);
    EXPECT_FALSE(fragments[0].encrypted);
    EXPECT_EQ(fragments[0].payload,
              std::vector<uint8_t>({
                  0x00,
                  static_cast<uint8_t>(ControlMessageType::VersionRequest),
                  0xAA, 0xBB
              }));
}

TEST(OutboundMessageEncoderTest, EstablishedCryptoMarksFrameEncrypted) {
    auto crypto = std::make_shared<EstablishedCrypto>();
    std::vector<std::vector<uint8_t>> wires;

    OutboundMessageEncoder encoder(
        crypto,
        [](const std::error_code&) {},
        [&wires](std::vector<uint8_t> wire) { wires.push_back(std::move(wire)); });

    encoder.send_message(2, static_cast<uint16_t>(MediaMessageType::Data), {0x11, 0x22});

    auto fragments = decode_frames(wires);
    ASSERT_EQ(fragments.size(), 1u);
    EXPECT_TRUE(fragments[0].encrypted);
    EXPECT_EQ(fragments[0].payload, std::vector<uint8_t>({0x00, 0x00, 0x11, 0x22}));
}

TEST(OutboundMessageEncoderTest, PlaintextControlMessageBypassesEncryptionAfterHandshake) {
    auto crypto = std::make_shared<EstablishedCrypto>();
    std::vector<std::vector<uint8_t>> wires;

    OutboundMessageEncoder encoder(
        crypto,
        [](const std::error_code&) {},
        [&wires](std::vector<uint8_t> wire) { wires.push_back(std::move(wire)); });

    encoder.send_plaintext_control_message(
        static_cast<uint16_t>(ControlMessageType::AuthComplete), {0x01});

    auto fragments = decode_frames(wires);
    ASSERT_EQ(fragments.size(), 1u);
    EXPECT_FALSE(fragments[0].encrypted);
    EXPECT_EQ(fragments[0].payload,
              std::vector<uint8_t>({
                  0x00,
                  static_cast<uint8_t>(ControlMessageType::AuthComplete),
                  0x01
              }));
}

TEST(OutboundMessageEncoderTest, EncryptFailureReportsError) {
    auto crypto = std::make_shared<FailingEncryptCrypto>();
    std::error_code error;
    std::vector<std::vector<uint8_t>> wires;

    OutboundMessageEncoder encoder(
        crypto,
        [&error](const std::error_code& ec) { error = ec; },
        [&wires](std::vector<uint8_t> wire) { wires.push_back(std::move(wire)); });

    encoder.send_message(1, static_cast<uint16_t>(MediaMessageType::Data), {0x10});

    EXPECT_TRUE(static_cast<bool>(error));
    EXPECT_TRUE(wires.empty());
}
