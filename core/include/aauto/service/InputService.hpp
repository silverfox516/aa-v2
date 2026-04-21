#pragma once

#include "aauto/service/ServiceBase.hpp"

namespace aauto::service {

struct InputServiceConfig {
    uint32_t touch_width  = 800;
    uint32_t touch_height = 480;
};

class InputService : public ServiceBase {
public:
    InputService(SendMessageFn send_fn, InputServiceConfig config);

    ServiceType type() const override { return ServiceType::InputSource; }
    void fill_config(aap_protobuf::service::ServiceConfiguration* config) override;
    void send_touch(int32_t x, int32_t y, int32_t action) override;

private:
    InputServiceConfig input_config_;
};

} // namespace aauto::service
