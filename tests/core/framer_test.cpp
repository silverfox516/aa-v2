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

class FramerTest : public ::testing::Test {
protected:
    Framer framer_;
    std::vector<AapFragment> received_;

    FragmentHandler handler() {
        return [this](AapFragment frag) {
            received_.push_back(std::move(frag));
        };
    }
};

// ===== Unfragmented frames =====

TEST_F(FramerTest, DecodeUnfragmentedFrame) {
    auto wire = make_wire_frame(0, FragInfo::Unfragmented, false, {0x00, 0x01, 0xAA});

    framer_.feed(wire.data(), wire.size(), handler());

    ASSERT_EQ(received_.size(), 1u);
    EXPECT_EQ(received_[0].channel_id, 0);
    EXPECT_TRUE(received_[0].is_first);
    EXPECT_TRUE(received_[0].is_last);
    EXPECT_FALSE(received_[0].encrypted);
    EXPECT_EQ(received_[0].payload, std::vector<uint8_t>({0x00, 0x01, 0xAA}));
}

TEST_F(FramerTest, DecodeEncryptedFlag) {
    auto wire = make_wire_frame(3, FragInfo::Unfragmented, true, {0x00, 0x05, 0x01});

    framer_.feed(wire.data(), wire.size(), handler());

    ASSERT_EQ(received_.size(), 1u);
    EXPECT_TRUE(received_[0].encrypted);
    EXPECT_EQ(received_[0].channel_id, 3);
}

TEST_F(FramerTest, DecodeMultipleFramesInOneFeed) {
    auto wire1 = make_wire_frame(0, FragInfo::Unfragmented, false, {0x00, 0x01});
    auto wire2 = make_wire_frame(1, FragInfo::Unfragmented, false, {0x00, 0x02});

    std::vector<uint8_t> combined;
    combined.insert(combined.end(), wire1.begin(), wire1.end());
    combined.insert(combined.end(), wire2.begin(), wire2.end());

    framer_.feed(combined.data(), combined.size(), handler());

    ASSERT_EQ(received_.size(), 2u);
    EXPECT_EQ(received_[0].channel_id, 0);
    EXPECT_EQ(received_[1].channel_id, 1);
}

TEST_F(FramerTest, DecodePartialHeader) {
    auto wire = make_wire_frame(0, FragInfo::Unfragmented, false, {0x00, 0x01, 0xFF});

    framer_.feed(wire.data(), 3, handler());
    EXPECT_EQ(received_.size(), 0u);

    framer_.feed(wire.data() + 3, wire.size() - 3, handler());
    ASSERT_EQ(received_.size(), 1u);
}

// ===== Fragmented frames =====

TEST_F(FramerTest, DecodeFragments) {
    auto wire1 = make_wire_frame(5, FragInfo::First, false, {0x01, 0x02, 0x03});
    auto wire2 = make_wire_frame(5, FragInfo::Continuation, false, {0x04, 0x05});
    auto wire3 = make_wire_frame(5, FragInfo::Last, false, {0x06});

    framer_.feed(wire1.data(), wire1.size(), handler());
    framer_.feed(wire2.data(), wire2.size(), handler());
    framer_.feed(wire3.data(), wire3.size(), handler());

    // Framer delivers each fragment individually
    ASSERT_EQ(received_.size(), 3u);
    EXPECT_TRUE(received_[0].is_first);
    EXPECT_FALSE(received_[0].is_last);
    EXPECT_FALSE(received_[1].is_first);
    EXPECT_FALSE(received_[1].is_last);
    EXPECT_FALSE(received_[2].is_first);
    EXPECT_TRUE(received_[2].is_last);
}

// ===== Encode =====

TEST_F(FramerTest, EncodeSmallFrame) {
    OutboundFrame out;
    out.channel_id = 2;
    out.flags = compute_frame_flags(2, 0x0001, false);
    out.payload = {0x00, 0x01, 0xDE, 0xAD};

    auto wire_frames = framer_.encode(out);

    ASSERT_EQ(wire_frames.size(), 1u);
    EXPECT_EQ(wire_frames[0][0], 2);
    uint16_t len = (static_cast<uint16_t>(wire_frames[0][2]) << 8) | wire_frames[0][3];
    EXPECT_EQ(len, 4u);
}

TEST_F(FramerTest, EncodeWithEncryptFlag) {
    OutboundFrame out;
    out.channel_id = 1;
    out.flags = compute_frame_flags(1, 0x8000, true);
    out.payload = {0x00, 0x01, 0xFF};

    auto wire_frames = framer_.encode(out);

    ASSERT_EQ(wire_frames.size(), 1u);
    EXPECT_NE(wire_frames[0][1] & 0x08, 0);
}

TEST_F(FramerTest, EncodeLargeFrameFragments) {
    OutboundFrame out;
    out.channel_id = 0;
    out.flags = compute_frame_flags(0, 0x0001, false);
    out.payload.resize(kMaxFramePayloadSize + 100, 0xAB);

    auto wire_frames = framer_.encode(out);

    ASSERT_EQ(wire_frames.size(), 2u);
    EXPECT_EQ(wire_frames[0][1] & 0x03, static_cast<uint8_t>(FragInfo::First));
    EXPECT_EQ(wire_frames[1][1] & 0x03, static_cast<uint8_t>(FragInfo::Last));
}

// ===== Round-trip =====

TEST_F(FramerTest, EncodeDecodeRoundTrip) {
    OutboundFrame out;
    out.channel_id = 7;
    out.flags = compute_frame_flags(7, 0x8002, false);
    out.payload = {0x80, 0x02, 0x01, 0x02, 0x03};

    auto wire_frames = framer_.encode(out);
    for (auto& wire : wire_frames) {
        framer_.feed(wire.data(), wire.size(), handler());
    }

    ASSERT_EQ(received_.size(), 1u);
    EXPECT_EQ(received_[0].channel_id, 7);
    EXPECT_EQ(received_[0].payload, out.payload);
}
