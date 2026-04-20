#define LOG_TAG "AA.Framer"

#include "aauto/session/Framer.hpp"
#include "aauto/utils/Logger.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace aauto::session {

Framer::Framer() {
    recv_buffer_.reserve(kMaxFramePayloadSize + kFrameHeaderSize);
}

Framer::~Framer() = default;

void Framer::reset() {
    recv_buffer_.clear();
    reassembly_.clear();
}

// ===== Decoding (receive path) =====

void Framer::feed(const uint8_t* data, std::size_t size,
                  FrameReceivedHandler on_frame) {
    if (size == 0 || !on_frame) return;

    recv_buffer_.insert(recv_buffer_.end(), data, data + size);

    std::error_code ec;
    while (try_parse_frame(on_frame, ec)) {
        if (ec) {
            on_frame(ec, {});
            reset();
            return;
        }
    }

    if (ec) {
        on_frame(ec, {});
        reset();
    }
}

bool Framer::try_parse_frame(FrameReceivedHandler& on_frame,
                             std::error_code& ec) {
    // Need at least the header
    if (recv_buffer_.size() < kFrameHeaderSize) {
        return false;
    }

    // Parse header
    uint8_t channel_id = recv_buffer_[0];
    uint8_t flags      = recv_buffer_[1];
    auto frag = static_cast<FragInfo>(flags & 0x03);
    bool encrypted = (flags & 0x08) != 0;
    uint16_t payload_length = (static_cast<uint16_t>(recv_buffer_[2]) << 8)
                            | static_cast<uint16_t>(recv_buffer_[3]);

    // Need full payload
    std::size_t total_frame_size = kFrameHeaderSize + payload_length;
    if (recv_buffer_.size() < total_frame_size) {
        return false;
    }

    // Extract payload
    const uint8_t* payload_ptr = recv_buffer_.data() + kFrameHeaderSize;

    // Handle fragmentation
    switch (frag) {
        case FragInfo::Unfragmented: {
            // Complete single-frame message
            if (payload_length < 2) {
                ec = make_error_code(AapErrc::FramingError);
                AA_LOG_E("unfragmented frame too short: %u bytes", payload_length);
                return false;
            }

            uint16_t msg_type = (static_cast<uint16_t>(payload_ptr[0]) << 8)
                              | static_cast<uint16_t>(payload_ptr[1]);

            AapFrame frame;
            frame.channel_id = channel_id;
            frame.encrypted = encrypted;
            frame.message_type = msg_type;
            frame.payload.assign(payload_ptr + 2, payload_ptr + payload_length);

            // Consume from recv_buffer_
            recv_buffer_.erase(recv_buffer_.begin(),
                               recv_buffer_.begin() + total_frame_size);

            on_frame({}, std::move(frame));
            return true;
        }

        case FragInfo::First: {
            auto& ctx = reassembly_[channel_id];
            if (ctx.in_progress) {
                AA_LOG_W("channel %u: new FIRST while reassembly in progress, discarding old",
                         channel_id);
            }
            ctx.buffer.assign(payload_ptr, payload_ptr + payload_length);
            ctx.in_progress = true;
            ctx.encrypted = encrypted;

            recv_buffer_.erase(recv_buffer_.begin(),
                               recv_buffer_.begin() + total_frame_size);
            return true;
        }

        case FragInfo::Continuation: {
            auto it = reassembly_.find(channel_id);
            if (it == reassembly_.end() || !it->second.in_progress) {
                AA_LOG_W("channel %u: CONTINUATION without FIRST, ignoring",
                         channel_id);
                recv_buffer_.erase(recv_buffer_.begin(),
                                   recv_buffer_.begin() + total_frame_size);
                return true;
            }

            auto& ctx = it->second;
            if (ctx.buffer.size() + payload_length > kMaxReassemblySize) {
                ec = make_error_code(AapErrc::FramingError);
                AA_LOG_E("channel %u: reassembly exceeds max size", channel_id);
                return false;
            }

            ctx.buffer.insert(ctx.buffer.end(),
                              payload_ptr, payload_ptr + payload_length);

            recv_buffer_.erase(recv_buffer_.begin(),
                               recv_buffer_.begin() + total_frame_size);
            return true;
        }

        case FragInfo::Last: {
            auto it = reassembly_.find(channel_id);
            if (it == reassembly_.end() || !it->second.in_progress) {
                AA_LOG_W("channel %u: LAST without FIRST, ignoring", channel_id);
                recv_buffer_.erase(recv_buffer_.begin(),
                                   recv_buffer_.begin() + total_frame_size);
                return true;
            }

            auto& ctx = it->second;
            ctx.buffer.insert(ctx.buffer.end(),
                              payload_ptr, payload_ptr + payload_length);
            ctx.in_progress = false;

            if (ctx.buffer.size() < 2) {
                ec = make_error_code(AapErrc::FramingError);
                AA_LOG_E("channel %u: reassembled frame too short", channel_id);
                return false;
            }

            uint16_t msg_type = (static_cast<uint16_t>(ctx.buffer[0]) << 8)
                              | static_cast<uint16_t>(ctx.buffer[1]);

            AapFrame frame;
            frame.channel_id = channel_id;
            frame.encrypted = ctx.encrypted;
            frame.message_type = msg_type;
            frame.payload.assign(ctx.buffer.begin() + 2, ctx.buffer.end());

            ctx.buffer.clear();

            recv_buffer_.erase(recv_buffer_.begin(),
                               recv_buffer_.begin() + total_frame_size);

            on_frame({}, std::move(frame));
            return true;
        }
    }

    ec = make_error_code(AapErrc::FramingError);
    return false;
}

// ===== Encoding (send path) =====

std::vector<std::vector<uint8_t>> Framer::encode(const OutboundFrame& frame) {
    std::vector<std::vector<uint8_t>> result;

    const auto& payload = frame.payload;
    uint8_t encrypt_flag = frame.encrypt ? 0x08 : 0x00;

    if (payload.size() <= kMaxFramePayloadSize) {
        // Single unfragmented frame
        std::vector<uint8_t> wire(kFrameHeaderSize + payload.size());
        wire[0] = frame.channel_id;
        wire[1] = static_cast<uint8_t>(FragInfo::Unfragmented) | encrypt_flag;
        wire[2] = static_cast<uint8_t>((payload.size() >> 8) & 0xFF);
        wire[3] = static_cast<uint8_t>(payload.size() & 0xFF);
        std::memcpy(wire.data() + kFrameHeaderSize, payload.data(), payload.size());
        result.push_back(std::move(wire));
    } else {
        // Fragmented
        std::size_t offset = 0;
        bool first = true;

        while (offset < payload.size()) {
            std::size_t remaining = payload.size() - offset;
            std::size_t chunk_size = std::min(remaining,
                                              static_cast<std::size_t>(kMaxFramePayloadSize));
            bool last = (offset + chunk_size >= payload.size());

            FragInfo frag;
            if (first) {
                frag = FragInfo::First;
                first = false;
            } else if (last) {
                frag = FragInfo::Last;
            } else {
                frag = FragInfo::Continuation;
            }

            std::vector<uint8_t> wire(kFrameHeaderSize + chunk_size);
            wire[0] = frame.channel_id;
            wire[1] = static_cast<uint8_t>(frag) | encrypt_flag;
            wire[2] = static_cast<uint8_t>((chunk_size >> 8) & 0xFF);
            wire[3] = static_cast<uint8_t>(chunk_size & 0xFF);
            std::memcpy(wire.data() + kFrameHeaderSize,
                        payload.data() + offset, chunk_size);

            result.push_back(std::move(wire));
            offset += chunk_size;
        }
    }

    return result;
}

} // namespace aauto::session
