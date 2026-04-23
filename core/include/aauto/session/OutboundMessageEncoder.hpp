#pragma once

#include "aauto/crypto/ICryptoStrategy.hpp"
#include "aauto/session/Framer.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace aauto::session {

class OutboundMessageEncoder {
public:
    using ErrorHandler = std::function<void(const std::error_code&)>;
    using EnqueueHandler = std::function<void(std::vector<uint8_t>)>;

    OutboundMessageEncoder(std::shared_ptr<crypto::ICryptoStrategy> crypto,
                           ErrorHandler on_error,
                           EnqueueHandler enqueue_frame);

    void send_message(uint8_t channel_id, uint16_t message_type,
                      const std::vector<uint8_t>& payload);
    void send_plaintext_control_message(uint16_t message_type,
                                        const std::vector<uint8_t>& payload);

private:
    std::vector<uint8_t> build_message_payload(
        uint16_t message_type,
        const std::vector<uint8_t>& payload) const;
    void queue_encoded_frame(uint8_t channel_id, uint16_t message_type,
                             bool encrypted, std::vector<uint8_t> payload);
    void encrypt_and_queue_frame(uint8_t channel_id, uint8_t flags,
                                 std::vector<uint8_t> plaintext);

    std::shared_ptr<crypto::ICryptoStrategy> crypto_;
    ErrorHandler on_error_;
    EnqueueHandler enqueue_frame_;
    Framer framer_;
};

} // namespace aauto::session
