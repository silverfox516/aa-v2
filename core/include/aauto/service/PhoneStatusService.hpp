#pragma once

#include "aauto/service/ServiceBase.hpp"

namespace aauto::service {

/// Phone status channel — battery, signal, call notifications.
/// Currently logs received messages.
class PhoneStatusService : public ServiceBase {
public:
    explicit PhoneStatusService(SendMessageFn send_fn);

    ServiceType type() const override { return ServiceType::PhoneStatus; }
    void fill_config(aap_protobuf::service::ServiceConfiguration* config) override;
};

} // namespace aauto::service
