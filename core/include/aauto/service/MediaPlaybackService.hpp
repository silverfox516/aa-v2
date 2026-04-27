#pragma once

#include "aauto/service/ServiceBase.hpp"

namespace aauto::service {

/// Media playback status channel — phone -> HU notifications about
/// the current track and playback state.
///
/// Inbound messages handled (parsed + logged):
///   - MEDIA_PLAYBACK_STATUS (32769): play/pause/stop, source, position
///   - MEDIA_PLAYBACK_METADATA (32771): song, artist, album, album art
///
/// Outbound MEDIA_PLAYBACK_INPUT (32770) — HU-side playback control —
/// is not implemented yet. Wire it up when there's a place in the UI
/// to drive it (steering wheel media keys, on-screen controls).
///
/// Note (G.0 / troubleshooting #22): this service was previously a
/// fill_config-only stub and ended up being the largest contributor
/// to the phone-side cadence throttle (76KB METADATA + 1s STATUS).
/// The presence of real handlers here is what makes it safe to
/// advertise the channel again — the phone now sees a responsive
/// endpoint instead of silence.
class MediaPlaybackService : public ServiceBase {
public:
    explicit MediaPlaybackService(SendMessageFn send_fn);

    ServiceType type() const override { return ServiceType::MediaPlayback; }
    void fill_config(aap_protobuf::service::ServiceConfiguration* config) override;
};

} // namespace aauto::service
