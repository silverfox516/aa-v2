#define LOG_TAG "AA.InboundAssembler"

#include "aauto/session/InboundMessageAssembler.hpp"
#include "aauto/utils/Logger.hpp"

namespace aauto::session {

InboundMessageAssembler::InboundMessageAssembler(
        std::shared_ptr<crypto::ICryptoStrategy> crypto,
        ErrorHandler on_error)
    : crypto_(std::move(crypto))
    , on_error_(std::move(on_error)) {}

void InboundMessageAssembler::feed(const uint8_t* data, std::size_t size,
                                   MessageHandler on_message) {
    if (size == 0 || !on_message) return;

    framer_.feed(data, size,
        [this, &on_message](AapFragment frag) {
            auto& payload = channel_payloads_[frag.channel_id];
            if (!append_fragment_payload(frag, payload) || !frag.is_last) {
                return;
            }

            AapMessage message{};
            if (!extract_complete_message(frag.channel_id, payload, message)) {
                return;
            }
            on_message(std::move(message));
        });
}

bool InboundMessageAssembler::append_fragment_payload(
        const AapFragment& frag,
        std::vector<uint8_t>& channel_payload) {
    if (frag.is_first) {
        channel_payload.clear();
    }

    if (frag.encrypted && crypto_->is_established()) {
        bool decrypt_ok = true;
        std::vector<uint8_t> plaintext;
        crypto_->decrypt(frag.payload.data(), frag.payload.size(),
            [this, &decrypt_ok, &plaintext](const std::error_code& ec,
                                            std::vector<uint8_t> pt) {
                if (ec) {
                    decrypt_ok = false;
                    on_error_(make_error_code(AapErrc::DecryptionFailed));
                    return;
                }
                plaintext = std::move(pt);
            });
        if (!decrypt_ok) {
            return false;
        }
        channel_payload.insert(channel_payload.end(),
                               plaintext.begin(), plaintext.end());
        return true;
    }

    channel_payload.insert(channel_payload.end(),
                           frag.payload.begin(), frag.payload.end());
    return true;
}

bool InboundMessageAssembler::extract_complete_message(
        uint8_t channel_id,
        std::vector<uint8_t>& channel_payload,
        AapMessage& message) {
    if (channel_payload.size() < 2) {
        AA_LOG_W("ch %u: message too short (%zu bytes), dropping",
                 channel_id, channel_payload.size());
        channel_payload.clear();
        return false;
    }

    message.channel_id = channel_id;
    message.message_type =
        (static_cast<uint16_t>(channel_payload[0]) << 8) | channel_payload[1];
    message.payload.assign(channel_payload.begin() + 2, channel_payload.end());
    channel_payload.clear();
    return true;
}

} // namespace aauto::session
