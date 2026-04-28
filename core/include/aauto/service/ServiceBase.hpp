#pragma once

#include "aauto/service/IService.hpp"
#include "aauto/utils/ProtocolConstants.hpp"
#include "aauto/utils/ProtocolUtil.hpp"
#include "aauto/utils/Logger.hpp"

#include <map>

namespace aauto::service {

/// Optional base class with message dispatch table.
/// Automatically handles CHANNEL_OPEN_REQUEST with SUCCESS response.
/// Services may use this or implement IService directly.
class ServiceBase : public IService {
public:
    using MessageHandler = std::function<void(const uint8_t*, std::size_t)>;

    explicit ServiceBase(SendMessageFn send_fn)
        : send_fn_(std::move(send_fn)) {}

    void set_channel(uint8_t channel_id) override { channel_id_ = channel_id; }
    uint8_t channel_id() const override { return channel_id_; }

    void on_channel_open(uint8_t channel_id) override {
        channel_id_ = channel_id;
    }

    void on_message(uint16_t message_type,
                    const uint8_t* payload,
                    std::size_t payload_size) override {
        // CHANNEL_OPEN_REQUEST: respond first, then notify service
        if (message_type ==
                static_cast<uint16_t>(ControlMessageType::ChannelOpenRequest)) {
            // Send ChannelOpenResponse BEFORE on_channel_open so the phone
            // sees the confirmation before any follow-up messages the service
            // might send (e.g., VideoFocusNotification, DrivingStatus).
            std::vector<uint8_t> resp = {0x08, 0x00};
            send(static_cast<uint16_t>(ControlMessageType::ChannelOpenResponse),
                 resp);
            on_channel_open(channel_id_);
            return;
        }

        auto it = handlers_.find(message_type);
        if (it != handlers_.end()) {
            it->second(payload, payload_size);
        } else {
            ::aauto::log_impl(::aauto::LogLevel::Warn,
                              "AA.ServiceBase",
                              "%-18s %-24s unhandled (%zu bytes)",
                              channel_name(channel_id_),
                              msg_type_name(channel_id_, message_type),
                              payload_size);
        }
    }

    void on_channel_close() override {
        channel_id_ = 0;
    }

    void fill_config(
        aap_protobuf::service::ServiceConfiguration* /*config*/) override {
    }

protected:
    void register_handler(uint16_t msg_type, MessageHandler handler) {
        handlers_[msg_type] = std::move(handler);
    }

    void send(uint16_t msg_type, const std::vector<uint8_t>& payload) {
        if (send_fn_) {
            send_fn_(channel_id_, msg_type, payload);
        }
    }

    uint8_t       channel_id_ = 0;
    SendMessageFn send_fn_;

private:
    std::map<uint16_t, MessageHandler> handlers_;
};

} // namespace aauto::service
