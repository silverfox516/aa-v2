#pragma once

#include "aauto/crypto/ICryptoStrategy.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace aauto::session {

class HandshakeCoordinator {
public:
    using SendControlMessageFn = std::function<void(uint16_t, const std::vector<uint8_t>&)>;
    using CompletionHandler = std::function<void()>;
    using ErrorHandler = std::function<void(const std::error_code&)>;

    HandshakeCoordinator(std::shared_ptr<crypto::ICryptoStrategy> crypto,
                         SendControlMessageFn send_control_message,
                         CompletionHandler on_complete,
                         ErrorHandler on_error);

    void on_version_response(const std::vector<uint8_t>& payload);
    void begin_ssl_handshake();
    void on_ssl_data_received(const uint8_t* data, std::size_t size);

private:
    void handle_handshake_step_result(const std::error_code& ec,
                                      crypto::HandshakeResult result);

    std::shared_ptr<crypto::ICryptoStrategy> crypto_;
    SendControlMessageFn send_control_message_;
    CompletionHandler on_complete_;
    ErrorHandler on_error_;
};

} // namespace aauto::session
