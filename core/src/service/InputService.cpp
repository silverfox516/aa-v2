#define LOG_TAG "AA.InputService"

#include "aauto/service/InputService.hpp"
#include "aauto/utils/Logger.hpp"
#include "aauto/utils/ProtocolConstants.hpp"

#include <aap_protobuf/service/Service.pb.h>
#include <aap_protobuf/service/inputsource/InputSourceService.pb.h>
#include <aap_protobuf/service/inputsource/message/InputReport.pb.h>
#include <aap_protobuf/service/inputsource/message/TouchEvent.pb.h>
#include <aap_protobuf/service/inputsource/message/PointerAction.pb.h>
#include <aap_protobuf/service/inputsource/message/TouchScreenType.pb.h>
#include <aap_protobuf/service/media/sink/message/KeyBindingRequest.pb.h>
#include <aap_protobuf/service/media/sink/message/KeyBindingResponse.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>

#include <chrono>

namespace aauto::service {

namespace pb_input = aap_protobuf::service::inputsource::message;
namespace pb_keybind = aap_protobuf::service::media::sink::message;

static std::vector<uint8_t> serialize(const google::protobuf::MessageLite& msg) {
    std::vector<uint8_t> buf(msg.ByteSize());
    msg.SerializeToArray(buf.data(), static_cast<int>(buf.size()));
    return buf;
}

InputService::InputService(SendMessageFn send_fn, InputServiceConfig config)
    : ServiceBase(std::move(send_fn)), input_config_(config) {

    register_handler(static_cast<uint16_t>(InputMessageType::KeyBindingRequest),
        [this](const uint8_t*, std::size_t) {
            AA_LOG_I("KeyBindingRequest received");
            pb_keybind::KeyBindingResponse resp;
            resp.set_status(aap_protobuf::shared::STATUS_SUCCESS);
            send(static_cast<uint16_t>(InputMessageType::KeyBindingResponse),
                 serialize(resp));
        });
}

void InputService::send_touch(int32_t x, int32_t y, int32_t action) {
    pb_input::InputReport report;

    auto ts = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    report.set_timestamp(static_cast<uint64_t>(ts));

    auto* touch = report.mutable_touch_event();
    touch->set_action(static_cast<pb_input::PointerAction>(action));
    touch->set_action_index(0);

    auto* ptr = touch->add_pointer_data();
    ptr->set_x(static_cast<uint32_t>(x));
    ptr->set_y(static_cast<uint32_t>(y));
    ptr->set_pointer_id(0);

    send(static_cast<uint16_t>(InputMessageType::InputReport), serialize(report));
}

void InputService::fill_config(
        aap_protobuf::service::ServiceConfiguration* config) {
    auto* input = config->mutable_input_source_service();
    auto* ts = input->add_touchscreen();
    ts->set_width(input_config_.touch_width);
    ts->set_height(input_config_.touch_height);
    ts->set_type(aap_protobuf::service::inputsource::message::CAPACITIVE);
}

} // namespace aauto::service
