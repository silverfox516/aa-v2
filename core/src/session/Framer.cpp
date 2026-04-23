#define LOG_TAG "AA.Framer"

#include "aauto/session/Framer.hpp"
#include "aauto/utils/Logger.hpp"

#include <algorithm>
#include <cstring>

namespace aauto::session {

Framer::Framer() {
    recv_buffer_.reserve(kMaxFramePayloadSize + kFrameHeaderSize);
}

Framer::~Framer() = default;

void Framer::reset() {
    recv_buffer_.clear();
}

// ===== Decoding (receive path) =====

void Framer::feed(const uint8_t* data, std::size_t size,
                  FragmentHandler on_fragment) {
    if (size == 0 || !on_fragment) return;

    recv_buffer_.insert(recv_buffer_.end(), data, data + size);

    while (try_parse_fragment(on_fragment)) {}
}

bool Framer::try_parse_fragment(FragmentHandler& on_fragment) {
    if (recv_buffer_.size() < kFrameHeaderSize) return false;

    uint8_t channel_id = recv_buffer_[0];
    uint8_t flags      = recv_buffer_[1];
    bool is_first  = (flags & 0x01) != 0;
    bool is_last   = (flags & 0x02) != 0;
    bool encrypted = (flags & 0x08) != 0;
    uint16_t payload_length = (static_cast<uint16_t>(recv_buffer_[2]) << 8)
                            | static_cast<uint16_t>(recv_buffer_[3]);

    std::size_t total_frame_size = kFrameHeaderSize + payload_length;

    if (recv_buffer_.size() < total_frame_size) return false;

    const uint8_t* payload_ptr = recv_buffer_.data() + kFrameHeaderSize;

    AapFragment frag;
    frag.channel_id = channel_id;
    frag.is_first   = is_first;
    frag.is_last    = is_last;
    frag.encrypted  = encrypted;
    frag.payload.assign(payload_ptr, payload_ptr + payload_length);

    recv_buffer_.erase(recv_buffer_.begin(),
                       recv_buffer_.begin() + total_frame_size);

    on_fragment(std::move(frag));
    return true;
}

// ===== Encoding (send path) =====

std::vector<std::vector<uint8_t>> Framer::encode(const OutboundFrame& frame) {
    std::vector<std::vector<uint8_t>> result;
    const auto& payload = frame.payload;

    if (payload.size() <= kMaxFramePayloadSize) {
        std::vector<uint8_t> wire(kFrameHeaderSize + payload.size());
        wire[0] = frame.channel_id;
        wire[1] = frame.flags;
        wire[2] = static_cast<uint8_t>((payload.size() >> 8) & 0xFF);
        wire[3] = static_cast<uint8_t>(payload.size() & 0xFF);
        std::memcpy(wire.data() + kFrameHeaderSize, payload.data(), payload.size());
        result.push_back(std::move(wire));
    } else {
        std::size_t offset = 0;
        bool first = true;

        while (offset < payload.size()) {
            std::size_t remaining = payload.size() - offset;
            std::size_t chunk_size = std::min(remaining,
                                              static_cast<std::size_t>(kMaxFramePayloadSize));
            bool last = (offset + chunk_size >= payload.size());

            FragInfo frag_info;
            if (first) {
                frag_info = FragInfo::First;
                first = false;
            } else if (last) {
                frag_info = FragInfo::Last;
            } else {
                frag_info = FragInfo::Continuation;
            }

            std::vector<uint8_t> wire(kFrameHeaderSize + chunk_size);
            wire[0] = frame.channel_id;
            wire[1] = (frame.flags & 0xFC) | static_cast<uint8_t>(frag_info);
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
