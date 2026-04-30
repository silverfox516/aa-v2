#define LOG_TAG "AA.PhoneStatusService"

#include "aauto/service/PhoneStatusService.hpp"
#include "aauto/utils/Logger.hpp"

#include <aap_protobuf/service/Service.pb.h>
#include <aap_protobuf/service/phonestatus/PhoneStatusMessageId.pb.h>
#include <aap_protobuf/service/phonestatus/message/PhoneStatus.pb.h>

namespace aauto::service {

namespace pb_ps  = aap_protobuf::service::phonestatus;
namespace pb_psm = aap_protobuf::service::phonestatus::message;

namespace {

const char* state_name(pb_psm::PhoneStatus::State s) {
    switch (s) {
        case pb_psm::PhoneStatus::UNKNOWN:     return "UNKNOWN";
        case pb_psm::PhoneStatus::IN_CALL:     return "IN_CALL";
        case pb_psm::PhoneStatus::ON_HOLD:     return "ON_HOLD";
        case pb_psm::PhoneStatus::INACTIVE:    return "INACTIVE";
        case pb_psm::PhoneStatus::INCOMING:    return "INCOMING";
        case pb_psm::PhoneStatus::CONFERENCED: return "CONFERENCED";
        case pb_psm::PhoneStatus::MUTED:       return "MUTED";
    }
    return "?";
}

} // namespace

PhoneStatusService::PhoneStatusService(SendMessageFn send_fn)
    : ServiceBase(std::move(send_fn)) {

    register_handler(
        static_cast<uint16_t>(pb_ps::PHONE_STATUS),
        [this](const uint8_t* data, std::size_t size) {
            pb_psm::PhoneStatus msg;
            if (!msg.ParseFromArray(data, static_cast<int>(size))) {
                AA_LOG_W("%-18s %-24s parse failed (%zu bytes)",
                         "phone.status", "PHONE_STATUS", size);
                return;
            }

            int32_t signal = msg.has_signal_strength()
                ? static_cast<int32_t>(msg.signal_strength())
                : -1;

            // Suppress no-change repeats: only log when the call list
            // changes shape (count or first-call state/number) or
            // signal strength changes.
            int32_t     first_state  = -1;
            std::string first_number;
            if (msg.calls_size() > 0) {
                first_state  = static_cast<int32_t>(msg.calls(0).phone_state());
                first_number = msg.calls(0).caller_number();
            }
            bool changed = !last_seen_
                || msg.calls_size()         != static_cast<int>(last_call_count_)
                || first_state              != last_first_state_
                || first_number             != last_first_number_
                || signal                   != last_signal_;

            if (changed) {
                AA_LOG_I("%-18s %-24s calls=%d signal=%d",
                         "phone.status", "PHONE_STATUS",
                         msg.calls_size(), signal);
                for (int i = 0; i < msg.calls_size(); ++i) {
                    const auto& c = msg.calls(i);
                    AA_LOG_I("%-18s %-24s   [%d] state=%s dur=%us"
                             " number=\"%s\" id=\"%s\" type=\"%s\""
                             " thumbnail=%zuB",
                             "phone.status", "PHONE_STATUS", i,
                             state_name(c.phone_state()),
                             c.call_duration_seconds(),
                             c.caller_number().c_str(),
                             c.caller_id().c_str(),
                             c.caller_number_type().c_str(),
                             c.caller_thumbnail().size());
                }
                last_seen_         = true;
                last_call_count_   = msg.calls_size();
                last_first_state_  = first_state;
                last_first_number_ = first_number;
                last_signal_       = signal;
            }

            if (status_cb_) {
                std::vector<Call> calls;
                calls.reserve(msg.calls_size());
                for (int i = 0; i < msg.calls_size(); ++i) {
                    const auto& c = msg.calls(i);
                    Call entry;
                    entry.state = static_cast<int32_t>(c.phone_state());
                    entry.duration_seconds = c.call_duration_seconds();
                    entry.number      = c.caller_number();
                    entry.caller_id   = c.caller_id();
                    entry.number_type = c.caller_number_type();
                    entry.thumbnail.assign(c.caller_thumbnail().begin(),
                                           c.caller_thumbnail().end());
                    calls.push_back(std::move(entry));
                }
                status_cb_(calls, signal);
            }
        });
}

void PhoneStatusService::fill_config(
        aap_protobuf::service::ServiceConfiguration* config) {
    config->mutable_phone_status_service();
    AA_LOG_I("phone status service configured");
}

} // namespace aauto::service
