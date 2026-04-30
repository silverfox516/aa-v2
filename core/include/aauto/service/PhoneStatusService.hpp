#pragma once

#include "aauto/service/ServiceBase.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace aauto::service {

/// Phone status channel — call notifications, signal strength.
///
/// Inbound:
///   - PHONE_STATUS (32769): repeated Call entries (state, caller_number,
///     caller_id, duration, thumbnail) + optional signal_strength.
///
/// Outbound (not yet implemented in Day 1):
///   - PHONE_STATUS_INPUT (32770): InstrumentClusterInput (ENTER=accept,
///     BACK=reject) + caller identification.
///
/// Note (G.0 / troubleshooting #22): the inbound handler is registered
/// up-front so the channel is "responsive" if the phone opens it. If
/// modern Android Auto turns out to bypass this channel like ch12
/// MediaBrowser (G.1), we still observe that fact deterministically —
/// either the phone never sends CHANNEL_OPEN_REQ for ch9, or it opens
/// the channel and we see PHONE_STATUS messages flow.
///
/// Audio routing for actual call audio is Bluetooth HFP, completely
/// separate from this channel — see proto comment in
/// PhoneStatusService.proto.
class PhoneStatusService : public ServiceBase {
public:
    /// Per-call state, mapping the proto enum (1=IN_CALL, 2=ON_HOLD,
    /// 3=INACTIVE, 4=INCOMING, 5=CONFERENCED, 6=MUTED, 0=UNKNOWN).
    struct Call {
        int32_t     state = 0;
        uint32_t    duration_seconds = 0;
        std::string number;       // E.164 or local
        std::string caller_id;    // contact display name
        std::string number_type;  // "mobile", "work", "home", etc.
        std::vector<uint8_t> thumbnail;
    };

    /// PHONE_STATUS callback. calls is the full call list (typically 0
    /// or 1 entry; 2+ for conference/3-way). signal_strength is 0..4
    /// (bars), -1 when not present in the message.
    using StatusCallback = std::function<void(
        const std::vector<Call>& calls, int32_t signal_strength)>;

    explicit PhoneStatusService(SendMessageFn send_fn);

    void set_status_callback(StatusCallback cb) { status_cb_ = std::move(cb); }

    ServiceType type() const override { return ServiceType::PhoneStatus; }
    void fill_config(aap_protobuf::service::ServiceConfiguration* config) override;

private:
    StatusCallback status_cb_;

    // Last-logged scalar fields. PhoneStatus is event-driven (call
    // state transitions, new caller) but the phone may resend the same
    // status periodically — suppress repeats so logcat is informative
    // only on real changes.
    bool        last_seen_         = false;
    std::size_t last_call_count_   = 0;
    int32_t     last_first_state_  = -1;
    std::string last_first_number_;
    int32_t     last_signal_       = -2;
};

} // namespace aauto::service
