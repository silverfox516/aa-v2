#define LOG_TAG "AA.PhoneStatusService"

#include "aauto/service/PhoneStatusService.hpp"
#include "aauto/utils/Logger.hpp"

#include <aap_protobuf/service/Service.pb.h>

namespace aauto::service {

PhoneStatusService::PhoneStatusService(SendMessageFn send_fn)
    : ServiceBase(std::move(send_fn)) {}

void PhoneStatusService::fill_config(
        aap_protobuf::service::ServiceConfiguration* config) {
    config->mutable_phone_status_service();
    AA_LOG_I("phone status service configured");
}

} // namespace aauto::service
