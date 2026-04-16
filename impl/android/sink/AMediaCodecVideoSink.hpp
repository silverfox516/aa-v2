#pragma once

#include "aauto/sink/IVideoSink.hpp"

#include <media/NdkMediaCodec.h>
#include <android/native_window.h>

#include <atomic>
#include <thread>

namespace aauto::impl {

/// IVideoSink implementation using Android NDK AMediaCodec.
/// Decodes H.264 NALUs and renders to an ANativeWindow (Surface).
class AMediaCodecVideoSink : public sink::IVideoSink {
public:
    AMediaCodecVideoSink();
    ~AMediaCodecVideoSink();

    /// Set the output surface. Must be called before on_configure().
    void set_surface(ANativeWindow* window);

    void on_configure(const sink::VideoConfig& config) override;
    void on_codec_config(const uint8_t* data, std::size_t size,
                         int64_t timestamp_us) override;
    void on_video_data(const uint8_t* data, std::size_t size,
                       int64_t timestamp_us) override;
    void on_stop() override;

private:
    bool submit_buffer(const uint8_t* data, std::size_t size,
                       int64_t timestamp_us, uint32_t flags);
    void output_loop();
    void release_codec();

    AMediaCodec* codec_ = nullptr;
    ANativeWindow* window_ = nullptr;
    std::atomic<bool> running_{false};
    std::thread output_thread_;
};

} // namespace aauto::impl
