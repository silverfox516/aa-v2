#pragma once

#include "aauto/sink/IVideoSink.hpp"

#include <functional>

namespace aauto::sink {

/// Video sink that forwards compressed data to a callback (e.g., AIDL IPC).
/// No decoding — just passes H.264 NALUs to the app process.
class CallbackVideoSink : public IVideoSink {
public:
    using DataCallback = std::function<void(const uint8_t* data, std::size_t size,
                                            int64_t timestamp_us, bool is_config)>;

    explicit CallbackVideoSink(DataCallback cb)
        : callback_(std::move(cb)) {}

    void set_native_window(void* /*window*/) override {}
    void on_configure(const VideoConfig& /*config*/) override {}

    void on_codec_config(const uint8_t* data, std::size_t size,
                         int64_t timestamp_us) override {
        if (callback_) callback_(data, size, timestamp_us, true);
    }

    void on_video_data(const uint8_t* data, std::size_t size,
                       int64_t timestamp_us) override {
        if (callback_) callback_(data, size, timestamp_us, false);
    }

    void on_stop() override {}

private:
    DataCallback callback_;
};

} // namespace aauto::sink
