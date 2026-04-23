#define LOG_TAG "AA.OutboundEncoder"

#include "aauto/session/OutboundMessageEncoder.hpp"
#include "aauto/utils/Logger.hpp"
#include "aauto/utils/ProtocolConstants.hpp"

namespace aauto::session {

OutboundMessageEncoder::OutboundMessageEncoder(
        std::shared_ptr<crypto::ICryptoStrategy> crypto,
        ErrorHandler on_error,
        EnqueueHandler enqueue_frame)
    : crypto_(std::move(crypto))
    , on_error_(std::move(on_error))
    , enqueue_frame_(std::move(enqueue_frame)) {}

std::vector<uint8_t> OutboundMessageEncoder::build_message_payload(
        uint16_t message_type,
        const std::vector<uint8_t>& payload) const {
    std::vector<uint8_t> full_payload;
    full_payload.reserve(2 + payload.size());
    full_payload.push_back(static_cast<uint8_t>((message_type >> 8) & 0xFF));
    full_payload.push_back(static_cast<uint8_t>(message_type & 0xFF));
    full_payload.insert(full_payload.end(), payload.begin(), payload.end());
    return full_payload;
}

void OutboundMessageEncoder::queue_encoded_frame(uint8_t channel_id,
                                                 uint16_t message_type,
                                                 bool encrypted,
                                                 std::vector<uint8_t> payload) {
    auto flags = compute_frame_flags(channel_id, message_type, encrypted);
    OutboundFrame frame{channel_id, flags, std::move(payload)};
    auto wire_frames = framer_.encode(frame);
    for (auto& wire : wire_frames) {
        enqueue_frame_(std::move(wire));
    }
}

void OutboundMessageEncoder::encrypt_and_queue_frame(uint8_t channel_id,
                                                     uint8_t flags,
                                                     std::vector<uint8_t> plaintext) {
    crypto_->encrypt(plaintext.data(), plaintext.size(),
        [this, channel_id, flags](const std::error_code& ec,
                                  std::vector<uint8_t> ciphertext) {
            if (ec) {
                AA_LOG_E("encrypt failed: %s", ec.message().c_str());
                on_error_(ec);
                return;
            }

            OutboundFrame frame{channel_id, flags, std::move(ciphertext)};
            auto wire_frames = framer_.encode(frame);
            for (auto& wire : wire_frames) {
                enqueue_frame_(std::move(wire));
            }
        });
}

void OutboundMessageEncoder::send_message(uint8_t channel_id, uint16_t message_type,
                                          const std::vector<uint8_t>& payload) {
    auto full_payload = build_message_payload(message_type, payload);

    if (crypto_->is_established()) {
        auto flags = compute_frame_flags(channel_id, message_type, true);
        encrypt_and_queue_frame(channel_id, flags, std::move(full_payload));
        return;
    }

    queue_encoded_frame(channel_id, message_type, false, std::move(full_payload));
}

void OutboundMessageEncoder::send_plaintext_control_message(
        uint16_t message_type,
        const std::vector<uint8_t>& payload) {
    queue_encoded_frame(kControlChannelId, message_type, false,
                        build_message_payload(message_type, payload));
}

} // namespace aauto::session
