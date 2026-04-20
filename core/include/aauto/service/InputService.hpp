#pragma once

#include "aauto/service/ServiceBase.hpp"

namespace aauto::service {

struct InputServiceConfig {
    uint32_t touch_width  = 800;
    uint32_t touch_height = 480;
};

class InputService : public ServiceBase {
public:
    InputService(SendMessageFn send_fn, InputServiceConfig config)
        : ServiceBase(std::move(send_fn)), input_config_(config) {}

    ServiceType type() const override { return ServiceType::InputSource; }
    void fill_config(aap_protobuf::service::ServiceConfiguration* config) override;

private:
    InputServiceConfig input_config_;
};

} // namespace aauto::service
