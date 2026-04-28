#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

namespace aauto::engine {

enum class SessionStatus : uint32_t {
    Connecting    = 0,
    Handshaking   = 1,
    Running       = 2,
    Disconnecting = 3,
    Disconnected  = 4,
    Error         = 5
};

/// Callback interface: engine -> app notifications.
/// Implemented by AidlEngineController / DbusEngineController.
class IEngineCallback {
public:
    virtual ~IEngineCallback() = default;

    virtual void on_session_state_changed(uint32_t session_id,
                                          SessionStatus status) = 0;
    virtual void on_session_error(uint32_t session_id,
                                  const std::error_code& ec,
                                  const std::string& detail) = 0;
    /// Phone identified after ServiceDiscovery.
    virtual void on_phone_identified(uint32_t session_id,
                                     const std::string& device_name,
                                     const std::string& instance_id) = 0;

    /// Compressed video data (H.264 NALUs) for app-side decoding.
    virtual void on_video_data(uint32_t session_id,
                               const uint8_t* data, std::size_t size,
                               int64_t timestamp_us, bool is_config) = 0;

    /// PCM audio data for app-side playback.
    virtual void on_audio_data(uint32_t session_id, uint32_t stream_type,
                               const uint8_t* data, std::size_t size,
                               int64_t timestamp_us) = 0;

    /// Phone requested video focus change (exit button, etc.)
    virtual void on_video_focus_changed(uint32_t session_id, bool projected) = 0;

    /// Playback status from media.playback channel (10).
    /// Default empty implementation so existing test mocks don't need
    /// updating until they care about playback.
    virtual void on_playback_status(uint32_t /*session_id*/,
                                    int32_t /*state*/,
                                    const std::string& /*media_source*/,
                                    uint32_t /*playback_seconds*/,
                                    bool /*shuffle*/,
                                    bool /*repeat*/,
                                    bool /*repeat_one*/) {}

    /// Playback metadata from media.playback channel (10).
    /// Default empty implementation; see on_playback_status note.
    virtual void on_playback_metadata(uint32_t /*session_id*/,
                                      const std::string& /*song*/,
                                      const std::string& /*artist*/,
                                      const std::string& /*album*/,
                                      const std::vector<uint8_t>& /*album_art*/,
                                      uint32_t /*duration_seconds*/) {}
};

/// Driving port: app -> engine commands.
///
/// Lifecycle:
///   1. Engine starts, exposes this interface via IPC
///   2. App registers callback
///   3. App calls start_session on phone discovery
///   4. App calls stop_session/stop_all on user action
///
/// Threading: methods may come from IPC threads.
/// Implementations post to engine's io_context internally.
class IEngineController {
public:
    virtual ~IEngineController() = default;

    /// Register callback. Caller must ensure cb outlives the engine.
    virtual void register_callback(IEngineCallback* cb) = 0;

    /// Create session for transport descriptor.
    /// Returns session_id. Connection proceeds asynchronously.
    virtual uint32_t start_session(const std::string& transport_descriptor) = 0;

    virtual void stop_session(uint32_t session_id) = 0;
    virtual void stop_all() = 0;
    virtual void set_active_session(uint32_t session_id) = 0;
    virtual void send_touch_event(uint32_t session_id,
                                  int32_t x, int32_t y, int32_t action) = 0;

    /// Attach a platform-native video surface to a session.
    /// Pass nullptr to detach. Platform layer casts to ANativeWindow* (Android)
    /// or equivalent. Core treats it as opaque pointer.
    virtual void set_video_surface(uint32_t session_id, void* native_window) = 0;

    /// Set video focus: true=PROJECTED (phone sends video), false=NATIVE
    virtual void set_video_focus(uint32_t session_id, bool projected) = 0;

    /// Attach/detach ALL sinks (video + audio). Used for session switching.
    virtual void attach_all_sinks(uint32_t session_id) = 0;
    virtual void detach_all_sinks(uint32_t session_id) = 0;

    /// Send a media-control KeyEvent (KEYCODE_MEDIA_*) via the input
    /// channel. Used for play/pause/next/prev buttons on the HU UI.
    virtual void send_media_key(uint32_t session_id, int32_t keycode) = 0;

    /// Send AudioFocus(LOSS) on the control channel. Phone's media
    /// app receives this as a focus loss and auto-pauses.
    virtual void release_audio_focus(uint32_t session_id) = 0;

    /// Send AudioFocus(GAIN). Used after release_audio_focus to allow
    /// the phone's media app to resume playback.
    virtual void gain_audio_focus(uint32_t session_id) = 0;
};

} // namespace aauto::engine
