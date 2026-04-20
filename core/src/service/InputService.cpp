#define LOG_TAG "AA.InputService"

#include "aauto/service/InputService.hpp"

#include <aap_protobuf/service/Service.pb.h>
#include <aap_protobuf/service/inputsource/InputSourceService.pb.h>
#include <aap_protobuf/service/inputsource/message/TouchScreenType.pb.h>

namespace aauto::service {

void InputService::fill_config(
        aap_protobuf::service::ServiceConfiguration* config) {
    auto* input = config->mutable_input_source_service();
    auto* ts = input->add_touchscreen();
    ts->set_width(input_config_.touch_width);
    ts->set_height(input_config_.touch_height);
    ts->set_type(aap_protobuf::service::inputsource::message::CAPACITIVE);
}

} // namespace aauto::service
