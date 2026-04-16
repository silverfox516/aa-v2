#pragma once

#include <cstdint>
#include <cstddef>

namespace aauto::sink {

struct AudioConfig {
    uint32_t sample_rate;     // 8000, 16000, 44100, 48000
    uint32_t bit_depth;       // 16, 24, 32
    uint32_t channel_count;   // 1, 2, 4, 6, 8
    uint32_t codec_type;      // MediaCodecType enum value
};

enum class AudioStreamType : uint32_t {
    Media    = 1,
    Guidance = 2,
    System   = 3,
    Call     = 4
};

/// Outbound port: receives audio data (PCM or compressed).
///
/// Lifecycle: same as IVideoSink. Threading: must not block.
class IAudioSink {
public:
    virtual ~IAudioSink() = default;

    virtual void on_configure(const AudioConfig& config,
                              AudioStreamType stream_type) = 0;
    virtual void on_codec_config(const uint8_t* data, std::size_t size) = 0;
    virtual void on_audio_data(const uint8_t* data, std::size_t size,
                               int64_t timestamp_us) = 0;
    virtual void on_stop() = 0;
};

} // namespace aauto::sink
