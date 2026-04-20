#define LOG_TAG "AA.VideoSink"

#include "AMediaCodecVideoSink.hpp"
#include "aauto/utils/Logger.hpp"

#include <media/NdkMediaFormat.h>

#include <cstring>

namespace aauto::impl {

AMediaCodecVideoSink::AMediaCodecVideoSink() = default;

AMediaCodecVideoSink::~AMediaCodecVideoSink() {
    on_stop();
}

void AMediaCodecVideoSink::set_surface(ANativeWindow* window) {
    window_ = window;
}

void AMediaCodecVideoSink::on_configure(const sink::VideoConfig& config) {
    release_codec();

    const char* mime = "video/avc";  // H.264
    // TODO: select mime based on config.codec_type (VP9, H265, etc.)

    codec_ = AMediaCodec_createDecoderByType(mime);
    if (!codec_) {
        AA_LOG_E("failed to create decoder for %s", mime);
        return;
    }

    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, mime);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH,
                          static_cast<int32_t>(config.width));
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT,
                          static_cast<int32_t>(config.height));

    media_status_t status = AMediaCodec_configure(
        codec_, format, window_, nullptr, 0);
    AMediaFormat_delete(format);

    if (status != AMEDIA_OK) {
        AA_LOG_E("AMediaCodec_configure failed: %d", status);
        release_codec();
        return;
    }

    status = AMediaCodec_start(codec_);
    if (status != AMEDIA_OK) {
        AA_LOG_E("AMediaCodec_start failed: %d", status);
        release_codec();
        return;
    }

    running_ = true;
    output_thread_ = std::thread(&AMediaCodecVideoSink::output_loop, this);

    AA_LOG_I("decoder configured: %ux%u %s", config.width, config.height, mime);
}

void AMediaCodecVideoSink::on_codec_config(const uint8_t* data,
                                           std::size_t size,
                                           int64_t timestamp_us) {
    submit_buffer(data, size, timestamp_us, AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG);
}

void AMediaCodecVideoSink::on_video_data(const uint8_t* data,
                                         std::size_t size,
                                         int64_t timestamp_us) {
    submit_buffer(data, size, timestamp_us, 0);
}

void AMediaCodecVideoSink::on_stop() {
    running_ = false;
    if (output_thread_.joinable()) {
        output_thread_.join();
    }
    release_codec();
    AA_LOG_I("decoder stopped");
}

bool AMediaCodecVideoSink::submit_buffer(const uint8_t* data,
                                         std::size_t size,
                                         int64_t timestamp_us,
                                         uint32_t flags) {
    if (!codec_ || !running_) return false;

    ssize_t buf_idx = AMediaCodec_dequeueInputBuffer(codec_, 5000);  // 5ms timeout
    if (buf_idx < 0) {
        AA_LOG_W("no input buffer available");
        return false;
    }

    std::size_t buf_size = 0;
    uint8_t* buf = AMediaCodec_getInputBuffer(codec_, buf_idx, &buf_size);
    if (!buf || buf_size < size) {
        AA_LOG_E("input buffer too small: %zu < %zu", buf_size, size);
        AMediaCodec_queueInputBuffer(codec_, buf_idx, 0, 0, 0, 0);
        return false;
    }

    std::memcpy(buf, data, size);
    media_status_t status = AMediaCodec_queueInputBuffer(
        codec_, buf_idx, 0, size, timestamp_us, flags);

    if (status != AMEDIA_OK) {
        AA_LOG_E("queueInputBuffer failed: %d", status);
        return false;
    }

    return true;
}

void AMediaCodecVideoSink::output_loop() {
    AA_LOG_D("output loop started");

    while (running_) {
        AMediaCodecBufferInfo info;
        ssize_t buf_idx = AMediaCodec_dequeueOutputBuffer(codec_, &info, 10000);  // 10ms

        if (buf_idx >= 0) {
            // Render to surface (true = render)
            AMediaCodec_releaseOutputBuffer(codec_, buf_idx, true);
        } else if (buf_idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat* fmt = AMediaCodec_getOutputFormat(codec_);
            AA_LOG_I("output format changed: %s", AMediaFormat_toString(fmt));
            AMediaFormat_delete(fmt);
        } else if (buf_idx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
            // No action needed for NDK API
        }
        // AMEDIACODEC_INFO_TRY_AGAIN_LATER: just loop again
    }

    AA_LOG_D("output loop exited");
}

void AMediaCodecVideoSink::release_codec() {
    if (codec_) {
        AMediaCodec_stop(codec_);
        AMediaCodec_delete(codec_);
        codec_ = nullptr;
    }
}

} // namespace aauto::impl
