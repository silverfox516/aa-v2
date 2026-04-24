#pragma once

#include "aauto/service/ServiceBase.hpp"

namespace aauto::service {

/// Navigation turn-by-turn status channel.
/// Receives navigation updates from phone (turn events, distance, status).
/// Currently logs received messages; future: display on instrument cluster.
class NavigationStatusService : public ServiceBase {
public:
    explicit NavigationStatusService(SendMessageFn send_fn);

    ServiceType type() const override { return ServiceType::NavigationStatus; }
    void fill_config(aap_protobuf::service::ServiceConfiguration* config) override;
};

} // namespace aauto::service
