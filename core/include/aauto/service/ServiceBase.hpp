#pragma once

#include "aauto/service/IService.hpp"

#include <map>

namespace aauto::service {

/// Optional base class with message dispatch table.
/// Services may use this or implement IService directly.
class ServiceBase : public IService {
public:
    using MessageHandler = std::function<void(const uint8_t*, std::size_t)>;

    explicit ServiceBase(SendMessageFn send_fn)
        : send_fn_(std::move(send_fn)) {}

    void on_channel_open(uint8_t channel_id) override {
        channel_id_ = channel_id;
    }

    void on_message(uint16_t message_type,
                    const uint8_t* payload,
                    std::size_t payload_size) override {
        auto it = handlers_.find(message_type);
        if (it != handlers_.end()) {
            it->second(payload, payload_size);
        }
    }

    void on_channel_close() override {
        channel_id_ = 0;
    }

    void fill_config(
        aap_protobuf::service::ServiceConfiguration* /*config*/) override {
        // Default: no config. Override in derived services.
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
