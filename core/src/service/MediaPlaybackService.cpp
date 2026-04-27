#define LOG_TAG "AA.MediaPlaybackService"

#include "aauto/service/MediaPlaybackService.hpp"
#include "aauto/utils/Logger.hpp"

#include <aap_protobuf/service/Service.pb.h>
#include <aap_protobuf/service/mediaplayback/MediaPlaybackStatusMessageId.pb.h>
#include <aap_protobuf/service/mediaplayback/message/MediaPlaybackStatus.pb.h>
#include <aap_protobuf/service/mediaplayback/message/MediaPlaybackMetadata.pb.h>

namespace aauto::service {

namespace pb_mp  = aap_protobuf::service::mediaplayback;
namespace pb_mpm = aap_protobuf::service::mediaplayback::message;

namespace {

const char* state_name(pb_mpm::MediaPlaybackStatus::State s) {
    switch (s) {
        case pb_mpm::MediaPlaybackStatus::STOPPED: return "STOPPED";
        case pb_mpm::MediaPlaybackStatus::PLAYING: return "PLAYING";
        case pb_mpm::MediaPlaybackStatus::PAUSED:  return "PAUSED";
    }
    return "?";
}

} // namespace

MediaPlaybackService::MediaPlaybackService(SendMessageFn send_fn)
    : ServiceBase(std::move(send_fn)) {

    register_handler(
        static_cast<uint16_t>(pb_mp::MEDIA_PLAYBACK_STATUS),
        [](const uint8_t* data, std::size_t size) {
            pb_mpm::MediaPlaybackStatus msg;
            if (!msg.ParseFromArray(data, static_cast<int>(size))) {
                AA_LOG_W("%-18s %-24s parse failed (%zu bytes)",
                         "media.playback", "PLAYBACK_STATUS", size);
                return;
            }
            AA_LOG_I("%-18s %-24s state=%s source=\"%s\" pos=%us"
                     " shuffle=%d repeat=%d repeat_one=%d",
                     "media.playback", "PLAYBACK_STATUS",
                     state_name(msg.state()),
                     msg.media_source().c_str(),
                     msg.playback_seconds(),
                     msg.shuffle()    ? 1 : 0,
                     msg.repeat()     ? 1 : 0,
                     msg.repeat_one() ? 1 : 0);
        });

    register_handler(
        static_cast<uint16_t>(pb_mp::MEDIA_PLAYBACK_METADATA),
        [](const uint8_t* data, std::size_t size) {
            pb_mpm::MediaPlaybackMetadata msg;
            if (!msg.ParseFromArray(data, static_cast<int>(size))) {
                AA_LOG_W("%-18s %-24s parse failed (%zu bytes)",
                         "media.playback", "PLAYBACK_METADATA", size);
                return;
            }
            // album_art is image bytes (PNG/JPEG, typically 50-90KB).
            // Log size only — full content is not useful in the log.
            AA_LOG_I("%-18s %-24s song=\"%s\" artist=\"%s\" album=\"%s\""
                     " duration=%us album_art=%zuB rating=%d",
                     "media.playback", "PLAYBACK_METADATA",
                     msg.song().c_str(),
                     msg.artist().c_str(),
                     msg.album().c_str(),
                     msg.duration_seconds(),
                     msg.album_art().size(),
                     msg.rating());
        });
}

void MediaPlaybackService::fill_config(
        aap_protobuf::service::ServiceConfiguration* config) {
    config->mutable_media_playback_service();
    AA_LOG_I("media playback service configured");
}

} // namespace aauto::service
