#include <gtest/gtest.h>

#include "aauto/session/InboundMessageAssembler.hpp"
#include "aauto/utils/ProtocolConstants.hpp"

#include "mock/MockCrypto.hpp"

#include <system_error>
#include <vector>

using namespace aauto;
using namespace aauto::session;
using namespace aauto::test;

namespace {

class EstablishedDecryptCrypto : public MockCrypto {
public:
    EstablishedDecryptCrypto() {
        handshake_step(nullptr, 0, [](const std::error_code&, crypto::HandshakeResult) {});
    }
};

class FailingDecryptCrypto : public crypto::ICryptoStrategy {
public:
    void handshake_step(const uint8_t*, std::size_t,
                        crypto::HandshakeStepHandler handler) override {
        handler({}, crypto::HandshakeResult{{}, true});
    }

    void encrypt(const uint8_t*, std::size_t, crypto::CryptoHandler handler) override {
        handler({}, {});
    }

    void decrypt(const uint8_t*, std::size_t, crypto::CryptoHandler handler) override {
        handler(std::make_error_code(std::errc::permission_denied), {});
    }

    bool is_established() const override { return true; }
    void reset() override {}
};

std::vector<uint8_t> make_wire_frame(uint8_t channel_id,
                                     FragInfo frag,
                                     bool encrypted,
                                     const std::vector<uint8_t>& payload) {
    uint8_t flags = static_cast<uint8_t>(frag) | (encrypted ? kFlagEncrypted : 0);
    uint16_t len = static_cast<uint16_t>(payload.size());

    std::vector<uint8_t> wire;
    wire.push_back(channel_id);
    wire.push_back(flags);
    wire.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    wire.push_back(static_cast<uint8_t>(len & 0xFF));
    wire.insert(wire.end(), payload.begin(), payload.end());
    return wire;
}

} // namespace

TEST(InboundMessageAssemblerTest, DecodesPlaintextMessage) {
    auto crypto = std::make_shared<MockCrypto>();
    InboundMessageAssembler assembler(crypto, [](const std::error_code&) {});
    std::vector<AapMessage> messages;

    auto wire = make_wire_frame(3, FragInfo::Unfragmented, false,
                                {0x80, 0x01, 0xAA, 0xBB});
    assembler.feed(wire.data(), wire.size(),
                   [&messages](AapMessage msg) { messages.push_back(std::move(msg)); });

    ASSERT_EQ(messages.size(), 1u);
    EXPECT_EQ(messages[0].channel_id, 3);
    EXPECT_EQ(messages[0].message_type, 0x8001);
    EXPECT_EQ(messages[0].payload, std::vector<uint8_t>({0xAA, 0xBB}));
}

TEST(InboundMessageAssemblerTest, ReassemblesEncryptedFragments) {
    auto crypto = std::make_shared<EstablishedDecryptCrypto>();
    InboundMessageAssembler assembler(crypto, [](const std::error_code&) {});
    std::vector<AapMessage> messages;

    auto wire1 = make_wire_frame(1, FragInfo::First, true, {0x80, 0x02, 0x01});
    auto wire2 = make_wire_frame(1, FragInfo::Last, true, {0x02, 0x03});

    assembler.feed(wire1.data(), wire1.size(),
                   [&messages](AapMessage msg) { messages.push_back(std::move(msg)); });
    assembler.feed(wire2.data(), wire2.size(),
                   [&messages](AapMessage msg) { messages.push_back(std::move(msg)); });

    ASSERT_EQ(messages.size(), 1u);
    EXPECT_EQ(messages[0].message_type, 0x8002);
    EXPECT_EQ(messages[0].payload, std::vector<uint8_t>({0x01, 0x02, 0x03}));
}

TEST(InboundMessageAssemblerTest, ShortPayloadIsDropped) {
    auto crypto = std::make_shared<MockCrypto>();
    InboundMessageAssembler assembler(crypto, [](const std::error_code&) {});
    bool called = false;

    auto wire = make_wire_frame(4, FragInfo::Unfragmented, false, {0xAA});
    assembler.feed(wire.data(), wire.size(),
                   [&called](AapMessage) { called = true; });

    EXPECT_FALSE(called);
}

TEST(InboundMessageAssemblerTest, DecryptFailureReportsErrorAndSuppressesMessage) {
    auto crypto = std::make_shared<FailingDecryptCrypto>();
    std::error_code error;
    InboundMessageAssembler assembler(
        crypto,
        [&error](const std::error_code& ec) { error = ec; });
    bool called = false;

    auto wire = make_wire_frame(2, FragInfo::Unfragmented, true, {0x80, 0x03, 0x55});
    assembler.feed(wire.data(), wire.size(),
                   [&called](AapMessage) { called = true; });

    EXPECT_EQ(error, make_error_code(AapErrc::DecryptionFailed));
    EXPECT_FALSE(called);
}
