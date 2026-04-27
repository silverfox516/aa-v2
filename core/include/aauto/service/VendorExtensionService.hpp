#pragma once

#include "aauto/service/ServiceBase.hpp"

#include <string>

namespace aauto::service {

/// Vendor extension channel — opaque vendor-specific message pipe
/// (e.g., OEM climate control, custom diagnostics). No standard
/// behavior; semantics defined by the vendor. Currently a stub.
class VendorExtensionService : public ServiceBase {
public:
    VendorExtensionService(SendMessageFn send_fn, std::string name);

    ServiceType type() const override { return ServiceType::VendorExtension; }
    void fill_config(aap_protobuf::service::ServiceConfiguration* config) override;

private:
    std::string name_;
};

} // namespace aauto::service
