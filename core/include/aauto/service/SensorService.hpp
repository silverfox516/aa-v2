#pragma once

#include "aauto/service/ServiceBase.hpp"

namespace aauto::service {

class SensorService : public ServiceBase {
public:
    explicit SensorService(SendMessageFn send_fn)
        : ServiceBase(std::move(send_fn)) {}

    ServiceType type() const override { return ServiceType::SensorSource; }
    void fill_config(aap_protobuf::service::ServiceConfiguration* config) override;
};

} // namespace aauto::service
