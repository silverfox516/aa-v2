#define LOG_TAG "AudioService"

#include "aauto/service/AudioService.hpp"
#include "aauto/utils/Logger.hpp"
#include "aauto/utils/ProtocolConstants.hpp"

namespace aauto::service {

AudioService::AudioService(SendMessageFn send_fn,
                           sink::AudioStreamType stream_type,
                           std::vector<std::shared_ptr<sink::IAudioSink>> sinks)
    : ServiceBase(std::move(send_fn))
    , stream_type_(stream_type)
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

void AudioService::on_channel_open(uint8_t channel_id) {
    ServiceBase::on_channel_open(channel_id);
    AA_LOG_I("audio channel opened: %u, stream_type=%u",
             channel_id, static_cast<uint32_t>(stream_type_));
}

void AudioService::on_channel_close() {
    if (started_) {
        for (auto& sink : sinks_) {
            sink->on_stop();
        }
        started_ = false;
    }
    ServiceBase::on_channel_close();
    AA_LOG_I("audio channel closed");
}

void AudioService::on_setup(const uint8_t* data, std::size_t size) {
    // TODO: parse Setup protobuf to extract codec type
    (void)data;
    (void)size;
    current_config_.codec_type = 1;  // AUDIO_PCM default
    AA_LOG_I("audio setup received");
}

void AudioService::on_config(const uint8_t* data, std::size_t size) {
    // TODO: parse Config protobuf for max_unacked, configuration indices
    (void)data;
    (void)size;
    max_unacked_ = 5;
    AA_LOG_I("audio config received, max_unacked=%u", max_unacked_);
}

void AudioService::on_start(const uint8_t* data, std::size_t size) {
    // TODO: parse Start protobuf for session_id, configuration_index
    (void)data;
    (void)size;
    started_ = true;
    unacked_count_ = 0;

    for (auto& sink : sinks_) {
        sink->on_configure(current_config_, stream_type_);
    }
    AA_LOG_I("audio start, stream_type=%u", static_cast<uint32_t>(stream_type_));
}

void AudioService::on_codec_config(const uint8_t* data, std::size_t size) {
    if (!started_) return;

    for (auto& sink : sinks_) {
        sink->on_codec_config(data, size);
    }
}

void AudioService::on_data(const uint8_t* data, std::size_t size) {
    if (!started_) return;

    int64_t timestamp_us = 0;  // TODO: extract from data

    for (auto& sink : sinks_) {
        sink->on_audio_data(data, size, timestamp_us);
    }

    unacked_count_++;
    if (unacked_count_ >= max_unacked_) {
        send_ack();
        unacked_count_ = 0;
    }
}

void AudioService::on_stop(const uint8_t* /*data*/, std::size_t /*size*/) {
    if (started_) {
        for (auto& sink : sinks_) {
            sink->on_stop();
        }
        started_ = false;
    }
    AA_LOG_I("audio stop");
}

void AudioService::send_ack() {
    std::vector<uint8_t> payload;
    send(static_cast<uint16_t>(MediaMessageType::Ack), payload);
}

} // namespace aauto::service
