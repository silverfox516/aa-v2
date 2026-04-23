#define LOG_TAG "AA.Handshake"

#include "aauto/session/HandshakeCoordinator.hpp"
#include "aauto/utils/Logger.hpp"
#include "aauto/utils/ProtocolConstants.hpp"

namespace aauto::session {

HandshakeCoordinator::HandshakeCoordinator(
        std::shared_ptr<crypto::ICryptoStrategy> crypto,
        SendControlMessageFn send_control_message,
        CompletionHandler on_complete,
        ErrorHandler on_error)
    : crypto_(std::move(crypto))
    , send_control_message_(std::move(send_control_message))
    , on_complete_(std::move(on_complete))
    , on_error_(std::move(on_error)) {}

void HandshakeCoordinator::on_version_response(const std::vector<uint8_t>& payload) {
    if (payload.size() >= 6) {
        uint16_t major  = (payload[0] << 8) | payload[1];
        uint16_t minor  = (payload[2] << 8) | payload[3];
        int16_t  status = static_cast<int16_t>((payload[4] << 8) | payload[5]);
        AA_LOG_I("VERSION_RESPONSE: v%u.%u status=%d", major, minor, status);
        if (status != 0) {
            AA_LOG_E("phone refused version (status=%d)", status);
            on_error_(make_error_code(AapErrc::VersionMismatch));
            return;
        }
    } else {
        AA_LOG_W("VERSION_RESPONSE short (%zu bytes), assuming OK", payload.size());
    }

    AA_LOG_I("version exchange complete, starting SSL handshake");
    begin_ssl_handshake();
}

void HandshakeCoordinator::begin_ssl_handshake() {
    crypto_->handshake_step(nullptr, 0,
        [this](const std::error_code& ec, crypto::HandshakeResult result) {
            handle_handshake_step_result(ec, std::move(result));
        });
}

void HandshakeCoordinator::on_ssl_data_received(const uint8_t* data, std::size_t size) {
    crypto_->handshake_step(data, size,
        [this](const std::error_code& ec, crypto::HandshakeResult result) {
            handle_handshake_step_result(ec, std::move(result));
        });
}

void HandshakeCoordinator::handle_handshake_step_result(
        const std::error_code& ec,
        crypto::HandshakeResult result) {
    if (ec) {
        on_error_(make_error_code(AapErrc::SslHandshakeFailed));
        return;
    }

    if (!result.output_bytes.empty()) {
        send_control_message_(
            static_cast<uint16_t>(ControlMessageType::EncapsulatedSsl),
            result.output_bytes);
    }

    if (result.complete) {
        on_complete_();
    }
}

} // namespace aauto::session
