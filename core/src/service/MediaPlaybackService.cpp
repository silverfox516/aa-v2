#define LOG_TAG "AA.MediaPlaybackService"

#include "aauto/service/MediaPlaybackService.hpp"
#include "aauto/utils/Logger.hpp"

#include <aap_protobuf/service/Service.pb.h>

namespace aauto::service {

MediaPlaybackService::MediaPlaybackService(SendMessageFn send_fn)
    : ServiceBase(std::move(send_fn)) {}

void MediaPlaybackService::fill_config(
        aap_protobuf::service::ServiceConfiguration* config) {
    config->mutable_media_playback_service();
    AA_LOG_I("media playback service configured");
}

} // namespace aauto::service
