#define LOG_TAG "AA.SensorService"

#include "aauto/service/SensorService.hpp"
#include "aauto/utils/Logger.hpp"
#include "aauto/utils/ProtocolConstants.hpp"

#include <aap_protobuf/service/Service.pb.h>
#include <aap_protobuf/service/sensorsource/SensorSourceService.pb.h>
#include <aap_protobuf/service/sensorsource/message/Sensor.pb.h>
#include <aap_protobuf/service/sensorsource/message/SensorType.pb.h>
#include <aap_protobuf/service/sensorsource/message/SensorBatch.pb.h>
#include <aap_protobuf/service/sensorsource/message/DrivingStatusData.pb.h>
#include <aap_protobuf/service/sensorsource/message/SensorRequest.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>

namespace aauto::service {

namespace pb_sensor = aap_protobuf::service::sensorsource::message;

static std::vector<uint8_t> serialize(const google::protobuf::MessageLite& msg) {
    std::vector<uint8_t> buf(msg.ByteSize());
    msg.SerializeToArray(buf.data(), static_cast<int>(buf.size()));
    return buf;
}

SensorService::SensorService(SendMessageFn send_fn)
    : ServiceBase(std::move(send_fn)) {

    using ST = SensorMessageType;

    register_handler(static_cast<uint16_t>(ST::Request),
        [this](const uint8_t* data, std::size_t size) {
            pb_sensor::SensorRequest req;
            if (req.ParseFromArray(data, static_cast<int>(size))) {
                AA_LOG_I("sensor start request: type=%d period=%lld",
                         req.type(),
                         static_cast<long long>(req.min_update_period()));

                if (req.type() == pb_sensor::SENSOR_DRIVING_STATUS_DATA) {
                    send_driving_status();
                }
            }
        });
}

void SensorService::on_channel_open(uint8_t channel_id) {
    ServiceBase::on_channel_open(channel_id);
    AA_LOG_I("sensor channel opened: %u", channel_id);
    send_driving_status();
}

void SensorService::send_driving_status() {
    pb_sensor::SensorBatch batch;
    auto* ds = batch.add_driving_status_data();
    ds->set_status(0);  // DRIVING_STATUS_UNRESTRICTED

    send(static_cast<uint16_t>(SensorMessageType::Batch), serialize(batch));
    AA_LOG_I("sent DrivingStatus(UNRESTRICTED)");
}

void SensorService::fill_config(
        aap_protobuf::service::ServiceConfiguration* config) {
    auto* sensor = config->mutable_sensor_source_service();
    auto* s1 = sensor->add_sensors();
    s1->set_sensor_type(pb_sensor::SENSOR_DRIVING_STATUS_DATA);
    auto* s2 = sensor->add_sensors();
    s2->set_sensor_type(pb_sensor::SENSOR_NIGHT_MODE);
}

} // namespace aauto::service
