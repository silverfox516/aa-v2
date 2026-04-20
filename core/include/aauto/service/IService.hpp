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

    virtual void on_channel_open(uint8_t channel_id) = 0;

    /// message_type: 2-byte type ID. payload: body after type stripped.
    virtual void on_message(uint16_t message_type,
                            const uint8_t* payload,
                            std::size_t payload_size) = 0;

    virtual void on_channel_close() = 0;

    virtual ServiceType type() const = 0;

    /// Fill this service's ServiceConfiguration for ServiceDiscoveryResponse.
    /// Each service knows its own proto configuration.
    virtual void fill_config(
        aap_protobuf::service::ServiceConfiguration* config) = 0;
};

} // namespace aauto::service
