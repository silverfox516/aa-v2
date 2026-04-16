#include <gtest/gtest.h>

#include "aauto/session/Framer.hpp"
#include "aauto/utils/ProtocolConstants.hpp"

#include <cstring>
#include <vector>

using namespace aauto;
using namespace aauto::session;

// Helper: build a raw wire frame from components.
static std::vector<uint8_t> make_wire_frame(uint8_t channel_id,
                                            FragInfo frag,
                                            bool encrypted,
                                            const std::vector<uint8_t>& payload) {
    uint8_t flags = static_cast<uint8_t>(frag) | (encrypted ? 0x08 : 0x00);
    uint16_t len = static_cast<uint16_t>(payload.size());

    std::vector<uint8_t> wire;
    wire.push_back(channel_id);
    wire.push_back(flags);
    wire.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    wire.push_back(static_cast<uint8_t>(len & 0xFF));
    wire.insert(wire.end(), payload.begin(), payload.end());
    return wire;
}

// Helper: build payload with 2-byte message type prefix.
static std::vector<uint8_t> make_payload(uint16_t msg_type,
                                         const std::vector<uint8_t>& body) {
    std::vector<uint8_t> payload;
    payload.push_back(static_cast<uint8_t>((msg_type >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>(msg_type & 0xFF));
    payload.insert(payload.end(), body.begin(), body.end());
    return payload;
}

class FramerTest : public ::testing::Test {
protected:
    Framer framer_;
    std::vector<AapFrame> received_frames_;
    std::vector<std::error_code> received_errors_;

    FrameReceivedHandler handler() {
        return [this](const std::error_code& ec, AapFrame frame) {
            if (ec) {
                received_errors_.push_back(ec);
            } else {
                received_frames_.push_back(std::move(frame));
            }
        };
    }
};

// ===== Unfragmented frames =====

TEST_F(FramerTest, DecodeUnfragmentedFrame) {
    // Arrange
    auto payload = make_payload(0x0001, {0xAA, 0xBB, 0xCC});
    auto wire = make_wire_frame(0, FragInfo::Unfragmented, false, payload);

    // Act
    framer_.feed(wire.data(), wire.size(), handler());

    // Assert
    ASSERT_EQ(received_frames_.size(), 1u);
    EXPECT_EQ(received_frames_[0].channel_id, 0);
    EXPECT_EQ(received_frames_[0].message_type, 0x0001);
    EXPECT_FALSE(received_frames_[0].encrypted);
    ASSERT_EQ(received_frames_[0].payload.size(), 3u);
    EXPECT_EQ(received_frames_[0].payload[0], 0xAA);
    EXPECT_EQ(received_frames_[0].payload[1], 0xBB);
    EXPECT_EQ(received_frames_[0].payload[2], 0xCC);
}

TEST_F(FramerTest, DecodeEncryptedFlag) {
    auto payload = make_payload(0x0005, {0x01});
    auto wire = make_wire_frame(3, FragInfo::Unfragmented, true, payload);

    framer_.feed(wire.data(), wire.size(), handler());

    ASSERT_EQ(received_frames_.size(), 1u);
    EXPECT_EQ(received_frames_[0].channel_id, 3);
    EXPECT_TRUE(received_frames_[0].encrypted);
    EXPECT_EQ(received_frames_[0].message_type, 0x0005);
}

TEST_F(FramerTest, DecodeMultipleFramesInOneFeed) {
    auto payload1 = make_payload(0x0001, {0x01});
    auto payload2 = make_payload(0x0002, {0x02});
    auto wire1 = make_wire_frame(0, FragInfo::Unfragmented, false, payload1);
    auto wire2 = make_wire_frame(1, FragInfo::Unfragmented, false, payload2);

    std::vector<uint8_t> combined;
    combined.insert(combined.end(), wire1.begin(), wire1.end());
    combined.insert(combined.end(), wire2.begin(), wire2.end());

    framer_.feed(combined.data(), combined.size(), handler());

    ASSERT_EQ(received_frames_.size(), 2u);
    EXPECT_EQ(received_frames_[0].message_type, 0x0001);
    EXPECT_EQ(received_frames_[1].message_type, 0x0002);
}

TEST_F(FramerTest, DecodePartialHeader) {
    auto payload = make_payload(0x0001, {0xFF});
    auto wire = make_wire_frame(0, FragInfo::Unfragmented, false, payload);

    // Feed header only (3 bytes, need 4)
    framer_.feed(wire.data(), 3, handler());
    EXPECT_EQ(received_frames_.size(), 0u);

    // Feed rest
    framer_.feed(wire.data() + 3, wire.size() - 3, handler());
    ASSERT_EQ(received_frames_.size(), 1u);
    EXPECT_EQ(received_frames_[0].message_type, 0x0001);
}

TEST_F(FramerTest, DecodePartialPayload) {
    auto payload = make_payload(0x0001, {0x01, 0x02, 0x03, 0x04, 0x05});
    auto wire = make_wire_frame(0, FragInfo::Unfragmented, false, payload);

    // Feed header + partial payload
    framer_.feed(wire.data(), 6, handler());
    EXPECT_EQ(received_frames_.size(), 0u);

    // Feed rest
    framer_.feed(wire.data() + 6, wire.size() - 6, handler());
    ASSERT_EQ(received_frames_.size(), 1u);
}

// ===== Fragmented frames =====

TEST_F(FramerTest, DecodeFragmentedFrame) {
    // A message split into 3 fragments
    std::vector<uint8_t> full_payload = make_payload(0x8001, {0x01, 0x02, 0x03, 0x04, 0x05, 0x06});

    auto frag1_data = std::vector<uint8_t>(full_payload.begin(), full_payload.begin() + 3);
    auto frag2_data = std::vector<uint8_t>(full_payload.begin() + 3, full_payload.begin() + 6);
    auto frag3_data = std::vector<uint8_t>(full_payload.begin() + 6, full_payload.end());

    auto wire1 = make_wire_frame(5, FragInfo::First, false, frag1_data);
    auto wire2 = make_wire_frame(5, FragInfo::Continuation, false, frag2_data);
    auto wire3 = make_wire_frame(5, FragInfo::Last, false, frag3_data);

    framer_.feed(wire1.data(), wire1.size(), handler());
    EXPECT_EQ(received_frames_.size(), 0u);

    framer_.feed(wire2.data(), wire2.size(), handler());
    EXPECT_EQ(received_frames_.size(), 0u);

    framer_.feed(wire3.data(), wire3.size(), handler());
    ASSERT_EQ(received_frames_.size(), 1u);
    EXPECT_EQ(received_frames_[0].channel_id, 5);
    EXPECT_EQ(received_frames_[0].message_type, 0x8001);
    EXPECT_EQ(received_frames_[0].payload.size(), 6u);
}

// ===== Encode =====

TEST_F(FramerTest, EncodeSmallFrame) {
    OutboundFrame out;
    out.channel_id = 2;
    out.encrypt = false;
    out.payload = make_payload(0x0001, {0xDE, 0xAD});

    auto wire_frames = framer_.encode(out);

    ASSERT_EQ(wire_frames.size(), 1u);
    auto& wire = wire_frames[0];
    EXPECT_EQ(wire[0], 2);  // channel
    EXPECT_EQ(wire[1] & 0x03, static_cast<uint8_t>(FragInfo::Unfragmented));
    EXPECT_EQ(wire[1] & 0x08, 0);  // not encrypted
    uint16_t len = (static_cast<uint16_t>(wire[2]) << 8) | wire[3];
    EXPECT_EQ(len, 4u);  // 2 (msg_type) + 2 (body)
}

TEST_F(FramerTest, EncodeWithEncryptFlag) {
    OutboundFrame out;
    out.channel_id = 1;
    out.encrypt = true;
    out.payload = {0x00, 0x01, 0xFF};

    auto wire_frames = framer_.encode(out);

    ASSERT_EQ(wire_frames.size(), 1u);
    EXPECT_NE(wire_frames[0][1] & 0x08, 0);  // encrypted flag set
}

TEST_F(FramerTest, EncodeLargeFrameFragments) {
    OutboundFrame out;
    out.channel_id = 0;
    out.encrypt = false;
    // Payload larger than kMaxFramePayloadSize (16384)
    out.payload.resize(kMaxFramePayloadSize + 100, 0xAB);

    auto wire_frames = framer_.encode(out);

    ASSERT_EQ(wire_frames.size(), 2u);
    // First fragment
    EXPECT_EQ(wire_frames[0][1] & 0x03, static_cast<uint8_t>(FragInfo::First));
    // Last fragment
    EXPECT_EQ(wire_frames[1][1] & 0x03, static_cast<uint8_t>(FragInfo::Last));
}

// ===== Round-trip =====

TEST_F(FramerTest, EncodeDecodeRoundTrip) {
    OutboundFrame out;
    out.channel_id = 7;
    out.encrypt = false;
    out.payload = make_payload(0x8002, {0x01, 0x02, 0x03, 0x04, 0x05});

    auto wire_frames = framer_.encode(out);

    for (auto& wire : wire_frames) {
        framer_.feed(wire.data(), wire.size(), handler());
    }

    ASSERT_EQ(received_frames_.size(), 1u);
    EXPECT_EQ(received_frames_[0].channel_id, 7);
    EXPECT_EQ(received_frames_[0].message_type, 0x8002);
    EXPECT_EQ(received_frames_[0].payload,
              std::vector<uint8_t>({0x01, 0x02, 0x03, 0x04, 0x05}));
}

TEST_F(FramerTest, EncodeDecodeLargeRoundTrip) {
    OutboundFrame out;
    out.channel_id = 3;
    out.encrypt = true;
    // Build large payload: [msg_type:2][body]
    std::vector<uint8_t> body(kMaxFramePayloadSize * 3, 0);
    for (std::size_t i = 0; i < body.size(); ++i) {
        body[i] = static_cast<uint8_t>(i & 0xFF);
    }
    out.payload = make_payload(0x0000, body);

    auto wire_frames = framer_.encode(out);
    EXPECT_GT(wire_frames.size(), 1u);

    for (auto& wire : wire_frames) {
        framer_.feed(wire.data(), wire.size(), handler());
    }

    ASSERT_EQ(received_frames_.size(), 1u);
    EXPECT_EQ(received_frames_[0].channel_id, 3);
    EXPECT_TRUE(received_frames_[0].encrypted);
    EXPECT_EQ(received_frames_[0].message_type, 0x0000);
    EXPECT_EQ(received_frames_[0].payload.size(), body.size());
    EXPECT_EQ(received_frames_[0].payload, body);
}

// ===== Error cases =====

TEST_F(FramerTest, TooShortPayloadReportsError) {
    // Unfragmented frame with only 1 byte payload (need at least 2 for msg_type)
    auto wire = make_wire_frame(0, FragInfo::Unfragmented, false, {0x01});

    framer_.feed(wire.data(), wire.size(), handler());

    EXPECT_EQ(received_frames_.size(), 0u);
    ASSERT_EQ(received_errors_.size(), 1u);
    EXPECT_EQ(received_errors_[0], make_error_code(AapErrc::FramingError));
}

TEST_F(FramerTest, ResetClearsState) {
    auto frag1_data = make_payload(0x0001, {0x01});
    auto wire1 = make_wire_frame(5, FragInfo::First, false, frag1_data);
    framer_.feed(wire1.data(), wire1.size(), handler());

    framer_.reset();

    // After reset, feeding a LAST without FIRST should be harmless
    auto wire_last = make_wire_frame(5, FragInfo::Last, false, {0x00, 0x01, 0x02});
    framer_.feed(wire_last.data(), wire_last.size(), handler());

    EXPECT_EQ(received_frames_.size(), 0u);
    EXPECT_EQ(received_errors_.size(), 0u);
}
