#define LOG_TAG "AA.BluetoothService"

#include "aauto/service/BluetoothService.hpp"
#include "aauto/utils/Logger.hpp"
#include "aauto/utils/ProtobufCompat.hpp"

#include <aap_protobuf/service/Service.pb.h>
#include <aap_protobuf/service/bluetooth/BluetoothMessageId.pb.h>
#include <aap_protobuf/service/bluetooth/message/BluetoothPairingMethod.pb.h>
#include <aap_protobuf/service/bluetooth/message/BluetoothPairingRequest.pb.h>
#include <aap_protobuf/service/bluetooth/message/BluetoothPairingResponse.pb.h>
#include <aap_protobuf/service/bluetooth/message/BluetoothAuthenticationData.pb.h>
#include <aap_protobuf/service/bluetooth/message/BluetoothAuthenticationResult.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>

namespace aauto::service {

namespace pb_bt    = aap_protobuf::service::bluetooth;
namespace pb_btm   = aap_protobuf::service::bluetooth::message;
namespace pb_shared = aap_protobuf::shared;

namespace {

const char* method_name(pb_btm::BluetoothPairingMethod m) {
    switch (m) {
        case pb_btm::BLUETOOTH_PAIRING_UNAVAILABLE:        return "UNAVAILABLE";
        case pb_btm::BLUETOOTH_PAIRING_OOB:                return "OOB";
        case pb_btm::BLUETOOTH_PAIRING_NUMERIC_COMPARISON: return "NUMERIC_COMPARISON";
        case pb_btm::BLUETOOTH_PAIRING_PASSKEY_ENTRY:      return "PASSKEY_ENTRY";
        case pb_btm::BLUETOOTH_PAIRING_PIN:                return "PIN";
    }
    return "?";
}

} // namespace

BluetoothService::BluetoothService(SendMessageFn send_fn, std::string car_address)
    : ServiceBase(std::move(send_fn))
    , car_address_(std::move(car_address)) {

    register_handler(
        static_cast<uint16_t>(pb_bt::BLUETOOTH_MESSAGE_PAIRING_REQUEST),
        [this](const uint8_t* data, std::size_t size) {
            pb_btm::BluetoothPairingRequest msg;
            if (!msg.ParseFromArray(data, static_cast<int>(size))) {
                AA_LOG_W("%-18s %-24s parse failed (%zu bytes)",
                         "bluetooth", "PAIRING_REQUEST", size);
                return;
            }
            AA_LOG_I("%-18s %-24s phone=\"%s\" method=%s",
                     "bluetooth", "PAIRING_REQUEST",
                     msg.phone_address().c_str(),
                     method_name(msg.pairing_method()));
            // Day 1: log only. No auto-response — see header comment.
            // Bluedroid bridge in Day 2/3 will drive send_pairing_response().
            if (pairing_cb_) {
                pairing_cb_(msg.phone_address(),
                            static_cast<PairingMethod>(msg.pairing_method()));
            }
        });

    register_handler(
        static_cast<uint16_t>(pb_bt::BLUETOOTH_MESSAGE_AUTHENTICATION_DATA),
        [this](const uint8_t* data, std::size_t size) {
            pb_btm::BluetoothAuthenticationData msg;
            if (!msg.ParseFromArray(data, static_cast<int>(size))) {
                AA_LOG_W("%-18s %-24s parse failed (%zu bytes)",
                         "bluetooth", "AUTH_DATA", size);
                return;
            }
            PairingMethod m = msg.has_pairing_method()
                ? static_cast<PairingMethod>(msg.pairing_method())
                : PairingMethod::Unavailable;
            AA_LOG_I("%-18s %-24s data=\"%s\" method=%s",
                     "bluetooth", "AUTH_DATA",
                     msg.auth_data().c_str(),
                     msg.has_pairing_method()
                         ? method_name(msg.pairing_method())
                         : "(unset)");
            if (auth_cb_) {
                auth_cb_(msg.auth_data(), m);
            }
        });
}

void BluetoothService::send_pairing_response(int32_t status,
                                             bool already_paired) {
    pb_btm::BluetoothPairingResponse resp;
    resp.set_status(static_cast<pb_shared::MessageStatus>(status));
    resp.set_already_paired(already_paired);
    auto buf = utils::serialize_to_vector(resp);
    AA_LOG_I("%-18s %-24s status=%d already_paired=%d",
             "bluetooth", "PAIRING_RESPONSE (TX)",
             status, already_paired ? 1 : 0);
    send(static_cast<uint16_t>(pb_bt::BLUETOOTH_MESSAGE_PAIRING_RESPONSE), buf);
}

void BluetoothService::send_auth_result(int32_t status) {
    pb_btm::BluetoothAuthenticationResult resp;
    resp.set_status(static_cast<pb_shared::MessageStatus>(status));
    auto buf = utils::serialize_to_vector(resp);
    AA_LOG_I("%-18s %-24s status=%d",
             "bluetooth", "AUTH_RESULT (TX)", status);
    send(static_cast<uint16_t>(pb_bt::BLUETOOTH_MESSAGE_AUTHENTICATION_RESULT), buf);
}

void BluetoothService::fill_config(
        aap_protobuf::service::ServiceConfiguration* config) {
    auto* bt = config->mutable_bluetooth_service();
    bt->set_car_address(car_address_);
    bt->add_supported_pairing_methods(pb_btm::BLUETOOTH_PAIRING_PIN);
    AA_LOG_I("bluetooth service configured: mac=%s", car_address_.c_str());
}

} // namespace aauto::service
