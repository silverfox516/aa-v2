#define LOG_TAG "AA.MediaBrowserService"

#include "aauto/service/MediaBrowserService.hpp"
#include "aauto/utils/Logger.hpp"

#include <aap_protobuf/service/Service.pb.h>

namespace aauto::service {

MediaBrowserService::MediaBrowserService(SendMessageFn send_fn)
    : ServiceBase(std::move(send_fn)) {}

void MediaBrowserService::fill_config(
        aap_protobuf::service::ServiceConfiguration* config) {
    config->mutable_media_browser_service();
    AA_LOG_I("media browser service configured");
}

} // namespace aauto::service
