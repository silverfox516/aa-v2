#pragma once

#include "aauto/sink/IAudioSink.hpp"

#include <functional>

namespace aauto::sink {

/// Audio sink that forwards PCM data to a callback (e.g., AIDL IPC).
class CallbackAudioSink : public IAudioSink {
public:
    using DataCallback = std::function<void(uint32_t stream_type,
                                            const uint8_t* data, std::size_t size,
                                            int64_t timestamp_us)>;

    explicit CallbackAudioSink(DataCallback cb)
        : callback_(std::move(cb)) {}

    void on_configure(const AudioConfig& /*config*/,
                      AudioStreamType stream_type) override {
        stream_type_ = stream_type;
    }

    void on_codec_config(const uint8_t* /*data*/, std::size_t /*size*/) override {}

    void on_audio_data(const uint8_t* data, std::size_t size,
                       int64_t timestamp_us) override {
        if (callback_) {
            callback_(static_cast<uint32_t>(stream_type_), data, size, timestamp_us);
        }
    }

    void on_stop() override {}

private:
    DataCallback callback_;
    AudioStreamType stream_type_ = AudioStreamType::Media;
};

} // namespace aauto::sink
