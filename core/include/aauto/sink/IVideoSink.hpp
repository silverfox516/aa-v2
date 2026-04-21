#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace aauto::sink {

struct VideoConfig {
    uint32_t width   = 0;
    uint32_t height  = 0;
    uint32_t fps     = 0;
    uint32_t density = 0;
    uint32_t codec_type = 0;  // MediaCodecType enum value
    std::vector<uint8_t> codec_data;  // SPS/PPS for H.264
};

/// Outbound port: receives video data (H.264 NALUs etc.)
///
/// Lifecycle:
///   1. Constructed (idle)
///   2. on_configure() -- sink prepares decoder
///   3. on_codec_config() -- SPS/PPS or equivalent
///   4. on_video_data() -- repeated NAL units
///   5. on_stop() -- release decoder resources
///   6. on_configure() may be called again (reconfiguration)
///
/// Threading: all on_* called from io_context thread.
/// Implementations must not block (queue to decoder thread if needed).
class IVideoSink {
public:
    virtual ~IVideoSink() = default;

    /// Set the platform-native rendering surface (e.g., ANativeWindow*).
    virtual void set_native_window(void* window) = 0;

    virtual void on_configure(const VideoConfig& config) = 0;
    virtual void on_codec_config(const uint8_t* data, std::size_t size,
                                 int64_t timestamp_us) = 0;
    virtual void on_video_data(const uint8_t* data, std::size_t size,
                               int64_t timestamp_us) = 0;
    virtual void on_stop() = 0;
};

} // namespace aauto::sink
