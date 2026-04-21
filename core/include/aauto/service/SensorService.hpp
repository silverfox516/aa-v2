#pragma once

#include "aauto/service/ServiceBase.hpp"

namespace aauto::service {

class SensorService : public ServiceBase {
public:
    explicit SensorService(SendMessageFn send_fn);

    ServiceType type() const override { return ServiceType::SensorSource; }
    void on_channel_open(uint8_t channel_id) override;
    void fill_config(aap_protobuf::service::ServiceConfiguration* config) override;

private:
    void send_driving_status();
};

} // namespace aauto::service
