#pragma once

#include "aauto/service/ServiceBase.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace aauto::service {

/// Media playback status channel — phone -> HU notifications about
/// the current track and playback state.
///
/// Inbound messages handled:
///   - MEDIA_PLAYBACK_STATUS (32769): play/pause/stop, source, position
///   - MEDIA_PLAYBACK_METADATA (32771): song, artist, album, album art
///
/// Outbound MEDIA_PLAYBACK_INPUT (32770) is intentionally NOT
/// implemented here. Per F.20, HU-side playback control (UI buttons,
/// steering-wheel media keys) is sent via the Input channel using
/// KEYCODE_MEDIA_* — broader phone-side compatibility than the
/// playback-service proto. See InputService::send_media_key.
///
/// Behavior is parse + invoke-callback. The callback recipients (set
/// via `set_status_callback` / `set_metadata_callback`) are responsible
/// for whatever the platform side wants to do with the data — typically
/// forward to the app over AIDL. The service itself stays platform-free.
///
/// Note (G.0 / troubleshooting #22): this service was previously a
/// fill_config-only stub and ended up being the largest contributor
/// to the phone-side cadence throttle. Having real handlers (even if
/// they only forward to a callback) is what keeps the phone happy.
class MediaPlaybackService : public ServiceBase {
public:
    /// PLAYBACK_STATUS callback: state matches the proto enum
    /// (1=STOPPED, 2=PLAYING, 3=PAUSED).
    using StatusCallback = std::function<void(
        int32_t state,
        const std::string& media_source,
        uint32_t playback_seconds,
        bool shuffle,
        bool repeat,
        bool repeat_one)>;

    /// PLAYBACK_METADATA callback: album_art is the raw image bytes
    /// (PNG/JPEG, can be 3KB-90KB). playlist is the current
    /// playlist/queue name when the source app provides it.
    using MetadataCallback = std::function<void(
        const std::string& song,
        const std::string& artist,
        const std::string& album,
        const std::vector<uint8_t>& album_art,
        const std::string& playlist,
        uint32_t duration_seconds)>;

    explicit MediaPlaybackService(SendMessageFn send_fn);

    void set_status_callback(StatusCallback cb)     { status_cb_   = std::move(cb); }
    void set_metadata_callback(MetadataCallback cb) { metadata_cb_ = std::move(cb); }

    ServiceType type() const override { return ServiceType::MediaPlayback; }
    void fill_config(aap_protobuf::service::ServiceConfiguration* config) override;

private:
    StatusCallback   status_cb_;
    MetadataCallback metadata_cb_;

    // Last-logged PLAYBACK_STATUS values. The phone sends STATUS at
    // ~1 Hz with only playback_seconds incrementing — this is heartbeat
    // noise. We log only when something other than position changes
    // (state, source, mode flags). Callbacks always fire regardless so
    // the UI keeps ticking the playhead.
    bool        last_status_seen_      = false;
    int32_t     last_status_state_     = -1;
    std::string last_status_source_;
    bool        last_status_shuffle_   = false;
    bool        last_status_repeat_    = false;
    bool        last_status_repeat_one_= false;
};

} // namespace aauto::service
