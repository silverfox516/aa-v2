#pragma once

#include "aauto/crypto/ICryptoStrategy.hpp"
#include "aauto/session/Framer.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace aauto::session {

class InboundMessageAssembler {
public:
    using MessageHandler = std::function<void(AapMessage)>;
    using ErrorHandler = std::function<void(const std::error_code&)>;

    InboundMessageAssembler(std::shared_ptr<crypto::ICryptoStrategy> crypto,
                            ErrorHandler on_error);

    void feed(const uint8_t* data, std::size_t size, MessageHandler on_message);

private:
    bool append_fragment_payload(const AapFragment& frag,
                                 std::vector<uint8_t>& channel_payload);
    bool extract_complete_message(uint8_t channel_id,
                                  std::vector<uint8_t>& channel_payload,
                                  AapMessage& message);

    std::shared_ptr<crypto::ICryptoStrategy> crypto_;
    ErrorHandler on_error_;
    Framer framer_;
    std::map<uint8_t, std::vector<uint8_t>> channel_payloads_;
};

} // namespace aauto::session
