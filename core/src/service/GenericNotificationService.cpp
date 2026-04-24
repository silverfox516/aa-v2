#define LOG_TAG "AA.NotificationService"

#include "aauto/service/GenericNotificationService.hpp"
#include "aauto/utils/Logger.hpp"

#include <aap_protobuf/service/Service.pb.h>

namespace aauto::service {

GenericNotificationService::GenericNotificationService(SendMessageFn send_fn)
    : ServiceBase(std::move(send_fn)) {}

void GenericNotificationService::fill_config(
        aap_protobuf::service::ServiceConfiguration* config) {
    config->mutable_generic_notification_service();
    AA_LOG_I("generic notification service configured");
}

} // namespace aauto::service
