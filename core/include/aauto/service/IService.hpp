#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>

namespace aap_protobuf::service { class ServiceConfiguration; }

namespace aauto::service {

enum class ServiceType : uint32_t {
    Control             = 0,
    MediaSink           = 1,
    MediaSource         = 2,
    InputSource         = 3,
    SensorSource        = 4,
    BluetoothService    = 5,
    NavigationStatus    = 6,
    PhoneStatus         = 7,
    MediaBrowser        = 8,
    MediaPlayback       = 9,
    RadioService        = 10,
    VendorExtension     = 11,
    GenericNotification = 12,
    WifiProjection      = 13
};

/// Callback for services to send messages through Session.
using SendMessageFn = std::function<void(uint8_t channel_id,
                                         uint16_t message_type,
                                         const std::vector<uint8_t>& payload)>;

/// Base interface for all AAP channel services.
///
/// Services receive SendMessageFn (not Session*) to enforce unidirectional
/// dependency and enable isolated testing.
class IService {
public:
    virtual ~IService() = default;

    /// Assign this service's channel number. Called by Engine before session start.
    virtual void set_channel(uint8_t channel_id) = 0;

    /// Return this service's assigned channel number.
    virtual uint8_t channel_id() const = 0;

    virtual void on_channel_open(uint8_t channel_id) = 0;

    /// message_type: 2-byte type ID. payload: body after type stripped.
    virtual void on_message(uint16_t message_type,
                            const uint8_t* payload,
                            std::size_t payload_size) = 0;

    virtual void on_channel_close() = 0;

    /// Called when the session is stopping gracefully.
    /// Services may perform cleanup (e.g., ControlService sends ByeBye).
    virtual void on_session_stop() {}

    /// Set a platform-native video surface. Only meaningful for video services.
    virtual void set_native_window(void* /*window*/) {}

    /// Send touch event. Only meaningful for input services.
    virtual void send_touch(int32_t /*x*/, int32_t /*y*/, int32_t /*action*/) {}

    /// Set video focus. Only meaningful for video services.
    virtual void set_video_focus(bool /*projected*/) {}

    /// Activate media sinks. Only ACTIVE session should have sinks attached.
    virtual void attach_sinks() {}
    virtual void detach_sinks() {}

    virtual ServiceType type() const = 0;

    /// Fill this service's ServiceConfiguration for ServiceDiscoveryResponse.
    /// Each service knows its own proto configuration.
    virtual void fill_config(
        aap_protobuf::service::ServiceConfiguration* config) = 0;
};

} // namespace aauto::service
