#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace aauto::source {

enum class SensorType : uint32_t {
    Location      = 1,
    Compass       = 2,
    Speed         = 3,
    Rpm           = 4,
    Odometer      = 5,
    Fuel          = 6,
    ParkingBrake  = 7,
    Gear          = 8,
    NightMode     = 10,
    DrivingStatus = 13,
    Gyroscope     = 20,
    GpsSatellite  = 21
};

struct SensorData {
    SensorType type;
    int64_t    timestamp_ns;
    std::vector<uint8_t> payload;  // serialized protobuf for specific sensor
};

using SensorDataCallback = std::function<void(const SensorData&)>;

/// Inbound port: vehicle/platform sensor data -> AAP sensor channel.
///
/// Lifecycle:
///   1. Constructed (idle)
///   2. start_sensor(type, interval_ms, cb) -- begin reporting
///   3. stop_sensor(type) -- stop specific sensor
///   4. stop_all() -- cleanup
class ISensorSource {
public:
    virtual ~ISensorSource() = default;

    virtual void start_sensor(SensorType type, uint32_t interval_ms,
                              SensorDataCallback cb) = 0;
    virtual void stop_sensor(SensorType type) = 0;
    virtual void stop_all() = 0;
};

} // namespace aauto::source
