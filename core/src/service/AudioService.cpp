#define LOG_TAG "AA.AudioService"

#include "aauto/service/AudioService.hpp"
#include "aauto/utils/Logger.hpp"
#include "aauto/utils/ProtocolConstants.hpp"

#include <aap_protobuf/service/Service.pb.h>
#include <aap_protobuf/service/media/sink/MediaSinkService.pb.h>
#include <aap_protobuf/service/media/shared/message/MediaCodecType.pb.h>
#include <aap_protobuf/service/media/shared/message/AudioConfiguration.pb.h>
#include <aap_protobuf/service/media/shared/message/Setup.pb.h>
#include <aap_protobuf/service/media/shared/message/Config.pb.h>
#include <aap_protobuf/service/media/shared/message/Start.pb.h>
#include <aap_protobuf/service/media/source/message/Ack.pb.h>
#include <aap_protobuf/service/media/sink/message/AudioStreamType.pb.h>

#include <cstring>

namespace aauto::service {

namespace pb_audio = aap_protobuf::service::media::sink::message;
namespace pb_media = aap_protobuf::service::media;

static std::vector<uint8_t> serialize(const google::protobuf::MessageLite& msg) {
    std::vector<uint8_t> buf(msg.ByteSize());
    msg.SerializeToArray(buf.data(), static_cast<int>(buf.size()));
    return buf;
}

static constexpr std::size_t kAudioTimestampBytes = 8;

static pb_audio::AudioStreamType
to_proto_stream_type(sink::AudioStreamType st) {
    switch (st) {
        case sink::AudioStreamType::Media:    return pb_audio::AUDIO_STREAM_MEDIA;
        case sink::AudioStreamType::Guidance: return pb_audio::AUDIO_STREAM_GUIDANCE;
        case sink::AudioStreamType::System:   return pb_audio::AUDIO_STREAM_SYSTEM_AUDIO;
        case sink::AudioStreamType::Call:     return pb_audio::AUDIO_STREAM_TELEPHONY;
    }
    return pb_audio::AUDIO_STREAM_MEDIA;
}

AudioService::AudioService(SendMessageFn send_fn,
                           AudioServiceConfig config,
                           std::vector<std::shared_ptr<sink::IAudioSink>> sinks)
    : ServiceBase(std::move(send_fn))
    , audio_config_(config)
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
             channel_id,
             static_cast<uint32_t>(audio_config_.stream_type));
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

void AudioService::fill_config(
        aap_protobuf::service::ServiceConfiguration* config) {
    namespace pb_media = aap_protobuf::service::media;

    auto* sink = config->mutable_media_sink_service();
    sink->set_available_type(pb_media::shared::message::MEDIA_CODEC_AUDIO_PCM);
    sink->set_audio_type(to_proto_stream_type(audio_config_.stream_type));
    auto* ac = sink->add_audio_configs();
    ac->set_sampling_rate(audio_config_.sample_rate);
    ac->set_number_of_bits(audio_config_.bit_depth);
    ac->set_number_of_channels(audio_config_.channel_count);
}

void AudioService::on_setup(const uint8_t* data, std::size_t size) {
    pb_media::shared::message::Setup setup;
    if (setup.ParseFromArray(data, static_cast<int>(size))) {
        AA_LOG_I("audio setup: codec=%d", setup.type());
    }

    pb_media::shared::message::Config config;
    config.set_status(pb_media::shared::message::Config::STATUS_READY);
    config.set_max_unacked(10);
    config.add_configuration_indices(0);

    send(static_cast<uint16_t>(MediaMessageType::Config), serialize(config));
    AA_LOG_I("sent audio config (READY, max_unacked=10)");
}

void AudioService::on_config(const uint8_t* data, std::size_t size) {
    pb_media::shared::message::Config config;
    if (config.ParseFromArray(data, static_cast<int>(size))) {
        if (config.has_max_unacked()) {
            max_unacked_ = config.max_unacked();
        }
        AA_LOG_I("audio config received, max_unacked=%u", max_unacked_);
    }
}

void AudioService::on_start(const uint8_t* data, std::size_t size) {
    pb_media::shared::message::Start start;
    if (start.ParseFromArray(data, static_cast<int>(size))) {
        session_id_ = start.session_id();
        AA_LOG_I("audio start: session_id=%d, stream_type=%u",
                 session_id_,
                 static_cast<uint32_t>(audio_config_.stream_type));
    }

    started_ = true;
    unacked_count_ = 0;

    for (auto& sink : sinks_) {
        sink->on_configure(current_config_, audio_config_.stream_type);
    }
}

void AudioService::on_codec_config(const uint8_t* data, std::size_t size) {
    if (!started_) return;
    for (auto& sink : sinks_) {
        sink->on_codec_config(data, size);
    }
}

void AudioService::on_data(const uint8_t* data, std::size_t size) {
    if (!started_) return;

    int64_t timestamp_us = 0;
    const uint8_t* pcm_data = data;
    std::size_t pcm_size = size;

    if (size > kAudioTimestampBytes) {
        std::memcpy(&timestamp_us, data, sizeof(timestamp_us));
        pcm_data = data + kAudioTimestampBytes;
        pcm_size = size - kAudioTimestampBytes;
    }

    for (auto& sink : sinks_) {
        sink->on_audio_data(pcm_data, pcm_size, timestamp_us);
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
    pb_media::source::message::Ack ack;
    ack.set_session_id(session_id_);
    ack.set_ack(1);
    send(static_cast<uint16_t>(MediaMessageType::Ack), serialize(ack));
}

} // namespace aauto::service
