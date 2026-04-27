#define LOG_TAG "AA.BluetoothService"

#include "aauto/service/BluetoothService.hpp"
#include "aauto/utils/Logger.hpp"

#include <aap_protobuf/service/Service.pb.h>
#include <aap_protobuf/service/bluetooth/message/BluetoothPairingMethod.pb.h>

namespace aauto::service {

namespace pb_bt = aap_protobuf::service::bluetooth::message;

BluetoothService::BluetoothService(SendMessageFn send_fn, std::string car_address)
    : ServiceBase(std::move(send_fn))
    , car_address_(std::move(car_address)) {}

void BluetoothService::fill_config(
        aap_protobuf::service::ServiceConfiguration* config) {
    auto* bt = config->mutable_bluetooth_service();
    bt->set_car_address(car_address_);
    bt->add_supported_pairing_methods(pb_bt::BLUETOOTH_PAIRING_PIN);
    AA_LOG_I("bluetooth service configured: mac=%s", car_address_.c_str());
}

} // namespace aauto::service
