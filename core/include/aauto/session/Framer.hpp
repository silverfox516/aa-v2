#pragma once

#include "aauto/utils/ProtocolConstants.hpp"

#include <cstdint>
#include <cstddef>
#include <functional>
#include <system_error>
#include <vector>

namespace aauto::session {

/// A single AAP fragment as received from the wire.
/// Framer does NOT reassemble or decrypt — caller handles both,
/// matching the aasdk per-fragment decrypt model.
struct AapFragment {
    uint8_t              channel_id;
    bool                 is_first;
    bool                 is_last;
    bool                 encrypted;
    std::vector<uint8_t> payload;   // raw ciphertext (or plaintext pre-SSL)
};

/// A complete reassembled + decrypted AAP message ready for dispatch.
struct AapMessage {
    uint8_t              channel_id;
    uint16_t             message_type;
    std::vector<uint8_t> payload;       // after message_type stripped
};

/// A frame to be sent on the wire.
struct OutboundFrame {
    uint8_t              channel_id;
    uint8_t              flags;         // pre-computed by Session
    std::vector<uint8_t> payload;       // [message_type:2][body] (encrypted or plain)
};

using FragmentHandler = std::function<void(AapFragment fragment)>;

/// AAP binary frame parser + encoder.
///
/// Receive path: parses wire bytes into individual fragments.
/// Does NOT reassemble or decrypt — caller does both per-fragment.
///
/// Send path: encodes outbound frames, fragmenting if needed.
class Framer {
public:
    Framer();
    ~Framer();

    /// Feed raw bytes from transport. Calls on_fragment for each parsed fragment.
    void feed(const uint8_t* data, std::size_t size,
              FragmentHandler on_fragment);

    /// Encode outbound frame into wire bytes. Fragments if needed.
    std::vector<std::vector<uint8_t>> encode(const OutboundFrame& frame);

    /// Reset state.
    void reset();

private:
    bool try_parse_fragment(FragmentHandler& on_fragment);

    std::vector<uint8_t> recv_buffer_;
};

} // namespace aauto::session
