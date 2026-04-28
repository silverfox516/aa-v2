#define LOG_TAG "AA.InputService"

#include "aauto/service/InputService.hpp"
#include "aauto/utils/Logger.hpp"
#include "aauto/utils/ProtobufCompat.hpp"
#include "aauto/utils/ProtocolConstants.hpp"

#include <aap_protobuf/service/Service.pb.h>
#include <aap_protobuf/service/inputsource/InputSourceService.pb.h>
#include <aap_protobuf/service/inputsource/message/InputReport.pb.h>
#include <aap_protobuf/service/inputsource/message/TouchEvent.pb.h>
#include <aap_protobuf/service/inputsource/message/PointerAction.pb.h>
#include <aap_protobuf/service/inputsource/message/TouchScreenType.pb.h>
#include <aap_protobuf/service/inputsource/message/KeyEvent.pb.h>
#include <aap_protobuf/service/media/sink/message/KeyBindingRequest.pb.h>
#include <aap_protobuf/service/media/sink/message/KeyBindingResponse.pb.h>
#include <aap_protobuf/service/media/sink/message/KeyCode.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>

#include <chrono>

namespace aauto::service {

namespace pb_input = aap_protobuf::service::inputsource::message;
namespace pb_keybind = aap_protobuf::service::media::sink::message;

template <typename T>
static std::vector<uint8_t> serialize(const T& msg) {
    return utils::serialize_to_vector(msg);
}

InputService::InputService(SendMessageFn send_fn, InputServiceConfig config)
    : ServiceBase(std::move(send_fn)), input_config_(config) {

    register_handler(static_cast<uint16_t>(InputMessageType::KeyBindingRequest),
        [this](const uint8_t*, std::size_t) {
            AA_LOG_I("%-18s %-24s -> SUCCESS", "input", "KEY_BINDING_REQ");
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

void InputService::send_media_key(int32_t keycode) {
    // Send a press+release pair on a single InputReport so the phone
    // sees a complete key event. Keycode values come from
    // aap_protobuf KeyCode.proto (KEYCODE_MEDIA_PLAY_PAUSE=85, etc.).
    pb_input::InputReport report;
    auto ts = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    report.set_timestamp(static_cast<uint64_t>(ts));

    auto* key_event = report.mutable_key_event();
    {
        auto* down = key_event->add_keys();
        down->set_keycode(static_cast<uint32_t>(keycode));
        down->set_down(true);
        down->set_metastate(0);
    }
    {
        auto* up = key_event->add_keys();
        up->set_keycode(static_cast<uint32_t>(keycode));
        up->set_down(false);
        up->set_metastate(0);
    }

    AA_LOG_I("%-18s %-24s keycode=%d", "input", "MEDIA_KEY", keycode);
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
