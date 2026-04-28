#define LOG_TAG "AA.VideoService"

#include "aauto/service/VideoService.hpp"
#include "aauto/utils/Logger.hpp"
#include "aauto/utils/ProtobufCompat.hpp"
#include "aauto/utils/ProtocolConstants.hpp"

#include <aap_protobuf/service/Service.pb.h>
#include <aap_protobuf/service/media/sink/MediaSinkService.pb.h>
#include <aap_protobuf/service/media/sink/message/DisplayType.pb.h>
#include <aap_protobuf/service/media/shared/message/MediaCodecType.pb.h>
#include <aap_protobuf/service/media/shared/message/Setup.pb.h>
#include <aap_protobuf/service/media/shared/message/Config.pb.h>
#include <aap_protobuf/service/media/shared/message/Start.pb.h>
#include <aap_protobuf/service/media/sink/message/VideoCodecResolutionType.pb.h>
#include <aap_protobuf/service/media/sink/message/VideoFrameRateType.pb.h>
#include <aap_protobuf/service/media/sink/message/VideoConfiguration.pb.h>
#include <aap_protobuf/service/media/video/message/VideoFocusNotification.pb.h>
#include <aap_protobuf/service/media/video/message/VideoFocusRequestNotification.pb.h>
#include <aap_protobuf/service/media/video/message/VideoFocusMode.pb.h>
#include <aap_protobuf/service/media/source/message/Ack.pb.h>

#include <cstring>

namespace aauto::service {

namespace pb_media = aap_protobuf::service::media;

template <typename T>
static std::vector<uint8_t> serialize(const T& msg) {
    return utils::serialize_to_vector(msg);
}

// AAP video MEDIA_DATA payload is prefixed by an 8-byte int64 timestamp (us).
static constexpr std::size_t kVideoTimestampBytes = 8;

VideoService::VideoService(SendMessageFn send_fn,
                           VideoServiceConfig config,
                           std::vector<std::shared_ptr<sink::IVideoSink>> sinks)
    : ServiceBase(std::move(send_fn))
    , video_config_(config)
    , sinks_(std::move(sinks)) {

    using MT = MediaMessageType;
    register_handler(static_cast<uint16_t>(MT::Setup),
                     [this](auto* d, auto s) { on_setup(d, s); });
    register_handler(static_cast<uint16_t>(MT::Start),
                     [this](auto* d, auto s) { on_start(d, s); });
    register_handler(static_cast<uint16_t>(MT::CodecConfig),
                     [this](auto* d, auto s) { on_codec_config(d, s); });
    register_handler(static_cast<uint16_t>(MT::Data),
                     [this](auto* d, auto s) { on_data(d, s); });
    register_handler(static_cast<uint16_t>(MT::Stop),
                     [](auto*, auto) {
                         AA_LOG_I("media stop received");
                     });
    register_handler(static_cast<uint16_t>(MT::VideoFocusRequest),
                     [this](const uint8_t* data, std::size_t size) {
                         namespace vfm = aap_protobuf::service::media::video::message;
                         vfm::VideoFocusRequestNotification req;
                         if (!req.ParseFromArray(data, static_cast<int>(size))) return;
                         auto mode_name = [](int m) -> const char* {
                             switch (m) {
                                 case 1: return "PROJECTED";
                                 case 2: return "NATIVE";
                                 case 3: return "NATIVE_TRANSIENT";
                                 case 4: return "PROJECTED_NO_INPUT";
                                 default: return "UNKNOWN";
                             }
                         };
                         auto reason_name = [](int r) -> const char* {
                             switch (r) {
                                 case 0: return "UNKNOWN";
                                 case 1: return "PHONE_SCREEN_OFF";
                                 case 2: return "LAUNCH_NATIVE";
                                 default: return "UNKNOWN";
                             }
                         };
                         AA_LOG_I("%-18s %-24s mode=%s reason=%s",
                                  "video", "FOCUS_REQUEST",
                                  mode_name(req.mode()),
                                  reason_name(req.reason()));
                         if (req.mode() == vfm::VIDEO_FOCUS_NATIVE) {
                             set_video_focus(false);
                             if (focus_callback_) focus_callback_(false);
                         } else if (req.mode() == vfm::VIDEO_FOCUS_PROJECTED) {
                             set_video_focus(true);
                             if (focus_callback_) focus_callback_(true);
                         }
                     });
    register_handler(static_cast<uint16_t>(MT::Ack),
                     [](auto*, auto) {});
}

void VideoService::on_channel_open(uint8_t channel_id) {
    ServiceBase::on_channel_open(channel_id);
    AA_LOG_I("%-18s %-24s ch=%u", "video", "CHANNEL_OPEN", channel_id);
}

void VideoService::set_native_window(void* window) {
    for (auto& sink : sinks_) {
        sink->set_native_window(window);
    }
    AA_LOG_I("%-18s %-24s", "video", window ? "SURFACE_ATTACHED" : "SURFACE_DETACHED");
}

void VideoService::on_channel_close() {
    if (started_) {
        for (auto& sink : sinks_) {
            sink->on_stop();
        }
        started_ = false;
    }
    ServiceBase::on_channel_close();
    AA_LOG_I("%-18s %-24s", "video", "CHANNEL_CLOSE");
}

void VideoService::on_setup(const uint8_t* data, std::size_t size) {
    pb_media::shared::message::Setup setup;
    if (setup.ParseFromArray(data, static_cast<int>(size))) {
        const char* codec_name = "unknown";
        switch (setup.type()) {
            case 1: codec_name = "PCM"; break;
            case 2: codec_name = "AAC"; break;
            case 3: codec_name = "H264_BP"; break;
            case 4: codec_name = "H264_HP"; break;
            case 5: codec_name = "VP8"; break;
            default: break;
        }
        AA_LOG_I("%-18s %-24s codec=%s", "video", "MEDIA_SETUP", codec_name);
    }

    pb_media::shared::message::Config config;
    config.set_status(pb_media::shared::message::Config::STATUS_READY);
    config.set_max_unacked(10);
    config.add_configuration_indices(0);

    send(static_cast<uint16_t>(MediaMessageType::Config), serialize(config));
}

void VideoService::on_start(const uint8_t* data, std::size_t size) {
    pb_media::shared::message::Start start;
    if (start.ParseFromArray(data, static_cast<int>(size))) {
        session_id_ = start.session_id();
        AA_LOG_I("%-18s %-24s session=%d config=%d",
                 "video", "MEDIA_START", session_id_, start.configuration_index());
    }

    started_ = true;

    for (auto& sink : sinks_) {
        sink->on_configure(current_config_);
    }
}

void VideoService::on_codec_config(const uint8_t* data, std::size_t size) {
    AA_LOG_I("%-18s %-24s %zu bytes", "video", "CODEC_CONFIG", size);

    current_config_.codec_data.assign(data, data + size);
    current_config_.width = video_config_.width;
    current_config_.height = video_config_.height;
    current_config_.fps = video_config_.fps;

    for (auto& sink : sinks_) {
        sink->on_codec_config(data, size, 0);
    }
    send_ack();
}

void VideoService::on_data(const uint8_t* data, std::size_t size) {
    if (!started_ || !sinks_active_) {
        if (!sinks_active_) {
            AA_LOG_W("%-18s %-24s %zu bytes (sinks inactive)", "video", "DATA_DROPPED", size);
        }
        send_ack();
        return;
    }

    int64_t timestamp_us = 0;
    const uint8_t* frame_data = data;
    std::size_t frame_size = size;

    if (size > kVideoTimestampBytes) {
        std::memcpy(&timestamp_us, data, sizeof(timestamp_us));
        frame_data = data + kVideoTimestampBytes;
        frame_size = size - kVideoTimestampBytes;
    }

    for (auto& sink : sinks_) {
        sink->on_video_data(frame_data, frame_size, timestamp_us);
    }

    // ACK every frame — phone uses credit-based flow control
    send_ack();
}

void VideoService::send_ack() {
    pb_media::source::message::Ack ack;
    ack.set_session_id(session_id_);
    ack.set_ack(1);
    send(static_cast<uint16_t>(MediaMessageType::Ack), serialize(ack));
}

void VideoService::set_video_focus(bool projected) {
    if (projected) attach_sinks();
    else           detach_sinks();
    send_video_focus(projected);
}

void VideoService::attach_sinks() {
    sinks_active_ = true;
    AA_LOG_I("%-18s sinks attached", "video");
}

void VideoService::detach_sinks() {
    sinks_active_ = false;
    AA_LOG_I("%-18s sinks detached", "video");
}

void VideoService::send_video_focus(bool gain) {
    namespace vf = pb_media::video::message;

    vf::VideoFocusNotification ntf;
    ntf.set_focus(gain ? vf::VIDEO_FOCUS_PROJECTED : vf::VIDEO_FOCUS_NATIVE);
    ntf.set_unsolicited(true);

    send(static_cast<uint16_t>(MediaMessageType::VideoFocusNotification),
         serialize(ntf));
}

void VideoService::fill_config(
        aap_protobuf::service::ServiceConfiguration* config) {
    auto* sink = config->mutable_media_sink_service();
    sink->set_available_type(pb_media::shared::message::MEDIA_CODEC_VIDEO_H264_BP);

    // Map width×height to protobuf resolution enum
    auto resolution = pb_media::sink::message::VIDEO_800x480;
    if (video_config_.width >= 2560)      resolution = pb_media::sink::message::VIDEO_2560x1440;
    else if (video_config_.width >= 1920) resolution = pb_media::sink::message::VIDEO_1920x1080;
    else if (video_config_.width >= 1280) resolution = pb_media::sink::message::VIDEO_1280x720;

    auto frame_rate = video_config_.fps >= 60
        ? pb_media::sink::message::VIDEO_FPS_60
        : pb_media::sink::message::VIDEO_FPS_30;

    auto* vc = sink->add_video_configs();
    vc->set_codec_resolution(resolution);
    vc->set_frame_rate(frame_rate);
    vc->set_density(video_config_.density);
    vc->set_width_margin(0);
    vc->set_height_margin(0);

    // Display type drives phone-side content routing — MAIN gets the
    // full AA UI, CLUSTER gets glanceable cards. proto2 default is
    // MAIN if unset; we always set it explicitly for clarity and so
    // both MAIN and CLUSTER instances of VideoService produce
    // self-describing ServiceDiscoveryResponse rows.
    sink->set_display_type(static_cast<
        aap_protobuf::service::media::sink::message::DisplayType>(
            static_cast<int32_t>(video_config_.display_type)));
}

} // namespace aauto::service
