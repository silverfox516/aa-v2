#pragma once

#include "aauto/service/ServiceBase.hpp"

namespace aauto::service {

/// Generic notification channel — app notifications from phone.
/// Currently logs received messages.
class GenericNotificationService : public ServiceBase {
public:
    explicit GenericNotificationService(SendMessageFn send_fn);

    ServiceType type() const override { return ServiceType::GenericNotification; }
    void fill_config(aap_protobuf::service::ServiceConfiguration* config) override;
};

} // namespace aauto::service
