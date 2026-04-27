#define LOG_TAG "AA.VendorExtensionService"

#include "aauto/service/VendorExtensionService.hpp"
#include "aauto/utils/Logger.hpp"

#include <aap_protobuf/service/Service.pb.h>

namespace aauto::service {

VendorExtensionService::VendorExtensionService(SendMessageFn send_fn, std::string name)
    : ServiceBase(std::move(send_fn))
    , name_(std::move(name)) {}

void VendorExtensionService::fill_config(
        aap_protobuf::service::ServiceConfiguration* config) {
    auto* ve = config->mutable_vendor_extension_service();
    ve->set_name(name_);
    AA_LOG_I("vendor extension service configured: name=%s", name_.c_str());
}

} // namespace aauto::service
