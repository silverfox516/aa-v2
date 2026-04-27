#pragma once

#include "aauto/service/ServiceBase.hpp"

namespace aauto::service {

/// Media browser channel — phone exposes its media library tree to HU
/// (e.g., Spotify folders, local music). HU is the browser client.
/// Currently a stub: advertises capability and logs incoming messages.
class MediaBrowserService : public ServiceBase {
public:
    explicit MediaBrowserService(SendMessageFn send_fn);

    ServiceType type() const override { return ServiceType::MediaBrowser; }
    void fill_config(aap_protobuf::service::ServiceConfiguration* config) override;
};

} // namespace aauto::service
