#include <gtest/gtest.h>

#include "aauto/session/Framer.hpp"
#include "aauto/utils/ProtocolConstants.hpp"

#include <cstring>
#include <vector>

using namespace aauto;
using namespace aauto::session;

// Helper: build a raw wire frame from components.
// For multi-first fragments (FragInfo::First, i.e., first of a multi-fragment
// message), inserts the 4-byte big-endian total_size field between the header
// and payload as required by the AAP wire protocol. `total_size` is ignored
// for non-multi-first frames.
static std::vector<uint8_t> make_wire_frame(uint8_t channel_id,
                                            FragInfo frag,
                                            bool encrypted,
                                            const std::vector<uint8_t>& payload,
                                            uint32_t total_size = 0) {
    uint8_t frag_bits = static_cast<uint8_t>(frag);
    bool is_first = (frag_bits & 0x01) != 0;
    bool is_last  = (frag_bits & 0x02) != 0;
    bool is_multi_first = is_first && !is_last;

    uint8_t flags = frag_bits | (encrypted ? 0x08 : 0x00);
    uint16_t len = static_cast<uint16_t>(payload.size());

    std::vector<uint8_t> wire;
    wire.push_back(channel_id);
    wire.push_back(flags);
    wire.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    wire.push_back(static_cast<uint8_t>(len & 0xFF));
    if (is_multi_first) {
        wire.push_back(static_cast<uint8_t>((total_size >> 24) & 0xFF));
        wire.push_back(static_cast<uint8_t>((total_size >> 16) & 0xFF));
        wire.push_back(static_cast<uint8_t>((total_size >> 8) & 0xFF));
        wire.push_back(static_cast<uint8_t>(total_size & 0xFF));
    }
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
    // Total reassembled payload size = 3 + 2 + 1 = 6 bytes.
    auto wire1 = make_wire_frame(5, FragInfo::First, false,
                                 {0x01, 0x02, 0x03}, /*total_size=*/6);
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

// Verifies that a payload large enough to require fragmentation can be
// encoded and then decoded back to the original byte sequence. Catches
// regressions where encode and decode disagree on the multi-first
// 4-byte total_size field layout.
TEST_F(FramerTest, EncodeDecodeMultiFragmentRoundTrip) {
    OutboundFrame out;
    out.channel_id = 5;
    out.flags = compute_frame_flags(5, 0x0001, false);
    out.payload.resize(kMaxFramePayloadSize + 1000);
    for (std::size_t i = 0; i < out.payload.size(); ++i) {
        out.payload[i] = static_cast<uint8_t>(i & 0xFF);
    }

    auto wire_frames = framer_.encode(out);
    ASSERT_GE(wire_frames.size(), 2u);

    for (auto& wire : wire_frames) {
        framer_.feed(wire.data(), wire.size(), handler());
    }

    ASSERT_EQ(received_.size(), wire_frames.size());

    std::vector<uint8_t> reassembled;
    for (auto& frag : received_) {
        reassembled.insert(reassembled.end(),
                           frag.payload.begin(), frag.payload.end());
    }
    EXPECT_EQ(reassembled.size(), out.payload.size());
    EXPECT_EQ(reassembled, out.payload);

    EXPECT_TRUE(received_.front().is_first);
    EXPECT_FALSE(received_.front().is_last);
    EXPECT_FALSE(received_.back().is_first);
    EXPECT_TRUE(received_.back().is_last);
}
