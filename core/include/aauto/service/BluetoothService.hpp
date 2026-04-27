#pragma once

#include "aauto/service/ServiceBase.hpp"

#include <string>

namespace aauto::service {

/// Bluetooth channel — advertises HU's BT MAC and supported pairing
/// methods so the phone can initiate HFP/A2DP pairing during the AAP
/// session. Currently a stub: only the advertisement is implemented;
/// the actual pairing flow requires platform BT stack integration.
class BluetoothService : public ServiceBase {
public:
    BluetoothService(SendMessageFn send_fn, std::string car_address);

    ServiceType type() const override { return ServiceType::BluetoothService; }
    void fill_config(aap_protobuf::service::ServiceConfiguration* config) override;

private:
    std::string car_address_;
};

} // namespace aauto::service
