#pragma once

#include "aauto/service/ServiceBase.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace aauto::service {

/// Bluetooth channel — phone-driven pairing coordination over AAP.
///
/// AAP responsibility (this service):
///   - Advertise HU's BT MAC + supported pairing methods in SDR
///   - Receive PairingRequest from phone (phone_address + method)
///   - Receive AuthenticationData (PIN/passkey/etc.) from phone
///   - Respond with PairingResponse / AuthenticationResult
///
/// NON-responsibility (separate Bluedroid stack):
///   - Actual BT radio pairing (BluetoothAdapter API on Java side)
///   - HFP-AG profile activation (call audio routing)
///   - Audio HAL integration (BT SCO -> car speakers)
///
/// Day 1 mode (current): passive observation. Inbound messages are
/// parsed and logged; outbound responses are NOT auto-sent. This
/// avoids misleading the phone (claiming SUCCESS without actually
/// pairing via Bluedroid). The two send_* methods exist as the API
/// surface for Day 2/3 wiring once a Bluedroid bridge is in place.
class BluetoothService : public ServiceBase {
public:
    /// Pairing method enum mirrors BluetoothPairingMethod proto:
    /// -1 = UNAVAILABLE, 1 = OOB, 2 = NUMERIC_COMPARISON,
    /// 3 = PASSKEY_ENTRY, 4 = PIN.
    enum class PairingMethod : int32_t {
        Unavailable        = -1,
        OutOfBand          = 1,
        NumericComparison  = 2,
        PasskeyEntry       = 3,
        Pin                = 4,
    };

    using PairingRequestCallback = std::function<void(
        const std::string& phone_address, PairingMethod method)>;
    using AuthDataCallback = std::function<void(
        const std::string& auth_data, PairingMethod method)>;

    BluetoothService(SendMessageFn send_fn, std::string car_address);

    void set_pairing_request_callback(PairingRequestCallback cb) {
        pairing_cb_ = std::move(cb);
    }
    void set_auth_data_callback(AuthDataCallback cb) {
        auth_cb_ = std::move(cb);
    }

    /// Outbound response: status maps to MessageStatus proto enum
    /// (0 = SUCCESS, negative for various failure codes; see
    /// shared/MessageStatus.proto).
    void send_pairing_response(int32_t status, bool already_paired);
    void send_auth_result(int32_t status);

    ServiceType type() const override { return ServiceType::BluetoothService; }
    void fill_config(aap_protobuf::service::ServiceConfiguration* config) override;

private:
    std::string             car_address_;
    PairingRequestCallback  pairing_cb_;
    AuthDataCallback        auth_cb_;
};

} // namespace aauto::service
