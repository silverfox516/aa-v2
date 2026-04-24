#pragma once

#include "aauto/service/ServiceBase.hpp"

namespace aauto::service {

/// Media playback status channel — now playing, track info, controls.
/// Currently logs received messages.
class MediaPlaybackService : public ServiceBase {
public:
    explicit MediaPlaybackService(SendMessageFn send_fn);

    ServiceType type() const override { return ServiceType::MediaPlayback; }
    void fill_config(aap_protobuf::service::ServiceConfiguration* config) override;
};

} // namespace aauto::service
