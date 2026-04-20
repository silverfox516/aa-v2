#define LOG_TAG "AA.SensorService"

#include "aauto/service/SensorService.hpp"

#include <aap_protobuf/service/Service.pb.h>
#include <aap_protobuf/service/sensorsource/SensorSourceService.pb.h>
#include <aap_protobuf/service/sensorsource/message/Sensor.pb.h>
#include <aap_protobuf/service/sensorsource/message/SensorType.pb.h>

namespace aauto::service {

void SensorService::fill_config(
        aap_protobuf::service::ServiceConfiguration* config) {
    namespace pb = aap_protobuf::service::sensorsource::message;

    auto* sensor = config->mutable_sensor_source_service();
    auto* s1 = sensor->add_sensors();
    s1->set_sensor_type(pb::SENSOR_DRIVING_STATUS_DATA);
    auto* s2 = sensor->add_sensors();
    s2->set_sensor_type(pb::SENSOR_NIGHT_MODE);
}

} // namespace aauto::service
