#pragma once

#include "aauto/utils/ProtocolConstants.hpp"

#include <cstdint>
#include <cstddef>
#include <functional>
#include <map>
#include <system_error>
#include <vector>

namespace aauto::session {

/// A fully reassembled AAP frame ready for decryption and dispatch.
struct AapFrame {
    uint8_t              channel_id;
    bool                 encrypted;
    uint16_t             message_type;  // first 2 bytes of payload
    std::vector<uint8_t> payload;       // full reassembled payload (after msg_type stripped)
};

/// A frame to be sent on the wire.
struct OutboundFrame {
    uint8_t              channel_id;
    bool                 encrypt;
    std::vector<uint8_t> payload;       // [message_type:2][body]
};

using FrameReceivedHandler = std::function<void(const std::error_code& ec,
                                                AapFrame frame)>;

/// AAP binary frame encoder/decoder with fragmentation support.
///
/// Wire format per frame:
///   Byte 0:    channel_id (uint8)
///   Byte 1:    flags
///              bits[0-1]: FragInfo (0=continuation, 1=first, 2=last, 3=unfragmented)
///              bit[3]:    encrypted flag
///   Byte 2-3:  payload_length (uint16 big-endian)
///   Byte 4...: payload (payload_length bytes)
///
/// Encrypt-then-frame: Session encrypts the logical payload, then Framer
/// fragments the ciphertext. On receive, Framer reassembles fragments, then
/// Session decrypts. Framer is crypto-unaware.
class Framer {
public:
    Framer();
    ~Framer();

    /// Feed raw bytes from transport. Calls on_frame for each complete frame.
    void feed(const uint8_t* data, std::size_t size,
              FrameReceivedHandler on_frame);

    /// Encode outbound frame into wire bytes. Fragments if needed.
    std::vector<std::vector<uint8_t>> encode(const OutboundFrame& frame);

    /// Reset all reassembly state.
    void reset();

private:
    static constexpr std::size_t kMaxReassemblySize = 512 * 1024;  // 512 KiB

    struct ReassemblyContext {
        std::vector<uint8_t> buffer;
        bool in_progress = false;
        bool encrypted   = false;
    };

    std::vector<uint8_t> recv_buffer_;
    std::map<uint8_t, ReassemblyContext> reassembly_;

    bool try_parse_frame(FrameReceivedHandler& on_frame,
                         std::error_code& ec);
};

} // namespace aauto::session
