#define LOG_TAG "AA.NavStatusService"

#include "aauto/service/NavigationStatusService.hpp"
#include "aauto/utils/Logger.hpp"

#include <aap_protobuf/service/Service.pb.h>

namespace aauto::service {

NavigationStatusService::NavigationStatusService(SendMessageFn send_fn)
    : ServiceBase(std::move(send_fn)) {
    // Log all received navigation messages for now
}

void NavigationStatusService::fill_config(
        aap_protobuf::service::ServiceConfiguration* config) {
    auto* nav = config->mutable_navigation_status_service();
    nav->set_minimum_interval_ms(500);
    nav->set_type(aap_protobuf::service::navigationstatus::
                  NavigationStatusService::ENUM);
    AA_LOG_I("navigation status service configured (ENUM mode)");
}

} // namespace aauto::service
