#define LOG_TAG "AA.VideoService"

#include "aauto/service/VideoService.hpp"
#include "aauto/utils/Logger.hpp"
#include "aauto/utils/ProtocolConstants.hpp"

namespace aauto::service {

VideoService::VideoService(SendMessageFn send_fn,
                           std::vector<std::shared_ptr<sink::IVideoSink>> sinks)
    : ServiceBase(std::move(send_fn))
    , sinks_(std::move(sinks)) {

    using MT = MediaMessageType;
    register_handler(static_cast<uint16_t>(MT::Setup),
                     [this](auto* d, auto s) { on_setup(d, s); });
    register_handler(static_cast<uint16_t>(MT::Config),
                     [this](auto* d, auto s) { on_config(d, s); });
    register_handler(static_cast<uint16_t>(MT::Start),
                     [this](auto* d, auto s) { on_start(d, s); });
    register_handler(static_cast<uint16_t>(MT::CodecConfig),
                     [this](auto* d, auto s) { on_codec_config(d, s); });
    register_handler(static_cast<uint16_t>(MT::Data),
                     [this](auto* d, auto s) { on_data(d, s); });
    register_handler(static_cast<uint16_t>(MT::Stop),
                     [this](auto* d, auto s) { on_stop(d, s); });
}

void VideoService::on_channel_open(uint8_t channel_id) {
    ServiceBase::on_channel_open(channel_id);
    AA_LOG_I("video channel opened: %u", channel_id);
}

void VideoService::on_channel_close() {
    if (started_) {
        for (auto& sink : sinks_) {
            sink->on_stop();
        }
        started_ = false;
    }
    ServiceBase::on_channel_close();
    AA_LOG_I("video channel closed");
}

void VideoService::on_setup(const uint8_t* data, std::size_t size) {
    // TODO: parse Setup protobuf to extract codec type
    // For now, default to H264
    (void)data;
    (void)size;
    current_config_.codec_type = 3;  // VIDEO_H264_BP
    AA_LOG_I("video setup received");
}

void VideoService::on_config(const uint8_t* data, std::size_t size) {
    // TODO: parse Config protobuf for max_unacked, configuration indices, status
    (void)data;
    (void)size;
    max_unacked_ = 5;  // reasonable default
    AA_LOG_I("video config received, max_unacked=%u", max_unacked_);
}

void VideoService::on_start(const uint8_t* data, std::size_t size) {
    // TODO: parse Start protobuf for session_id, configuration_index
    (void)data;
    (void)size;
    started_ = true;
    unacked_count_ = 0;

    for (auto& sink : sinks_) {
        sink->on_configure(current_config_);
    }
    AA_LOG_I("video start, session_id=%d", session_id_);
}

void VideoService::on_codec_config(const uint8_t* data, std::size_t size) {
    if (!started_) {
        AA_LOG_W("codec_config before start, ignoring");
        return;
    }

    for (auto& sink : sinks_) {
        sink->on_codec_config(data, size, 0);
    }
    AA_LOG_D("video codec_config, %zu bytes", size);
}

void VideoService::on_data(const uint8_t* data, std::size_t size) {
    if (!started_) return;

    // TODO: extract timestamp from first 8 bytes if present
    int64_t timestamp_us = 0;

    for (auto& sink : sinks_) {
        sink->on_video_data(data, size, timestamp_us);
    }

    unacked_count_++;
    if (unacked_count_ >= max_unacked_) {
        send_ack();
        unacked_count_ = 0;
    }
}

void VideoService::on_stop(const uint8_t* /*data*/, std::size_t /*size*/) {
    if (started_) {
        for (auto& sink : sinks_) {
            sink->on_stop();
        }
        started_ = false;
    }
    AA_LOG_I("video stop");
}

void VideoService::send_ack() {
    // TODO: build Ack protobuf with session_id
    std::vector<uint8_t> payload;  // empty for now
    send(static_cast<uint16_t>(MediaMessageType::Ack), payload);
}

} // namespace aauto::service
