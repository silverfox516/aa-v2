#include <gtest/gtest.h>

#include "aauto/engine/HeadunitConfig.hpp"
#include "aauto/service/ControlService.hpp"
#include "aauto/utils/ProtocolConstants.hpp"

#include <aap_protobuf/service/Service.pb.h>
#include <aap_protobuf/service/media/shared/message/MediaCodecType.pb.h>
#include <aap_protobuf/service/control/message/AudioFocusNotification.pb.h>
#include <aap_protobuf/service/control/message/AudioFocusRequest.pb.h>
#include <aap_protobuf/service/control/message/ByeByeResponse.pb.h>
#include <aap_protobuf/service/control/message/PingRequest.pb.h>
#include <aap_protobuf/service/control/message/PingResponse.pb.h>
#include <aap_protobuf/service/control/message/ServiceDiscoveryRequest.pb.h>
#include <aap_protobuf/service/control/message/ServiceDiscoveryResponse.pb.h>

#include <cstdint>
#include <asio.hpp>
#include <memory>
#include <utility>
#include <vector>

using namespace aauto;
using namespace aauto::service;

namespace pb_ctrl = aap_protobuf::service::control::message;

namespace {

std::vector<uint8_t> serialize(const google::protobuf::MessageLite& msg) {
    std::vector<uint8_t> buf(msg.ByteSizeLong());
    msg.SerializeToArray(buf.data(), static_cast<int>(buf.size()));
    return buf;
}

class FakePeerService : public IService {
public:
    explicit FakePeerService(ServiceType type) : type_(type) {}

    void set_channel(uint8_t channel_id) override { channel_id_ = channel_id; }
    uint8_t channel_id() const override { return channel_id_; }
    void on_channel_open(uint8_t channel_id) override { channel_id_ = channel_id; }
    void on_message(uint16_t, const uint8_t*, std::size_t) override {}
    void on_channel_close() override {}
    ServiceType type() const override { return type_; }

    void fill_config(aap_protobuf::service::ServiceConfiguration* config) override {
        auto* sink = config->mutable_media_sink_service();
        sink->set_available_type(
            aap_protobuf::service::media::shared::message::MEDIA_CODEC_AUDIO_PCM);
    }

private:
    uint8_t channel_id_ = 0;
    ServiceType type_;
};

struct SentMessage {
    uint8_t channel_id;
    uint16_t message_type;
    std::vector<uint8_t> payload;
};

class ControlServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto peer = std::make_shared<FakePeerService>(ServiceType::MediaSink);
        peer->set_channel(1);
        peer_services_.emplace(1, peer);

        service_ = std::make_unique<ControlService>(
            io_.get_executor(),
            [this](uint8_t channel_id, uint16_t message_type,
                   const std::vector<uint8_t>& payload) {
                sent_.push_back({channel_id, message_type, payload});
            },
            config_,
            peer_services_);
        service_->set_channel(kControlChannelId);
    }

    SentMessage take_last_message() {
        EXPECT_FALSE(sent_.empty());
        return sent_.back();
    }

    engine::HeadunitConfig config_;
    asio::io_context io_;
    std::map<int32_t, std::shared_ptr<IService>> peer_services_;
    std::unique_ptr<ControlService> service_;
    std::vector<SentMessage> sent_;
};

} // namespace

TEST_F(ControlServiceTest, ServiceDiscoveryRequestBuildsResponseWithConfiguredChannel) {
    pb_ctrl::ServiceDiscoveryRequest req;
    req.set_device_name("Pixel 9");
    req.set_label_text("phone");

    const auto payload = serialize(req);
    service_->on_message(
        static_cast<uint16_t>(ControlMessageType::ServiceDiscoveryRequest),
        payload.data(),
        payload.size());

    const auto sent = take_last_message();
    EXPECT_EQ(sent.channel_id, kControlChannelId);
    EXPECT_EQ(sent.message_type,
              static_cast<uint16_t>(ControlMessageType::ServiceDiscoveryResponse));

    pb_ctrl::ServiceDiscoveryResponse resp;
    ASSERT_TRUE(resp.ParseFromArray(sent.payload.data(),
                                    static_cast<int>(sent.payload.size())));
    EXPECT_EQ(resp.display_name(), config_.display_name);
    ASSERT_EQ(resp.channels_size(), 1);
    EXPECT_EQ(resp.channels(0).id(), 1);
    EXPECT_TRUE(resp.channels(0).has_media_sink_service());
}

TEST_F(ControlServiceTest, PingRequestEchoesTimestamp) {
    pb_ctrl::PingRequest req;
    req.set_timestamp(123456789);

    const auto payload = serialize(req);
    service_->on_message(static_cast<uint16_t>(ControlMessageType::PingRequest),
                         payload.data(),
                         payload.size());

    const auto sent = take_last_message();
    EXPECT_EQ(sent.message_type,
              static_cast<uint16_t>(ControlMessageType::PingResponse));

    pb_ctrl::PingResponse resp;
    ASSERT_TRUE(resp.ParseFromArray(sent.payload.data(),
                                    static_cast<int>(sent.payload.size())));
    EXPECT_EQ(resp.timestamp(), req.timestamp());
}

TEST_F(ControlServiceTest, AudioFocusRequestProducesNotification) {
    pb_ctrl::AudioFocusRequest req;
    req.set_audio_focus_type(pb_ctrl::AUDIO_FOCUS_GAIN_TRANSIENT_MAY_DUCK);

    const auto payload = serialize(req);
    service_->on_message(static_cast<uint16_t>(ControlMessageType::AudioFocusRequest),
                         payload.data(),
                         payload.size());

    const auto sent = take_last_message();
    EXPECT_EQ(sent.message_type,
              static_cast<uint16_t>(ControlMessageType::AudioFocusNotification));

    pb_ctrl::AudioFocusNotification notif;
    ASSERT_TRUE(notif.ParseFromArray(sent.payload.data(),
                                     static_cast<int>(sent.payload.size())));
    EXPECT_EQ(notif.focus_state(),
              pb_ctrl::AUDIO_FOCUS_STATE_GAIN_TRANSIENT_GUIDANCE_ONLY);
    EXPECT_FALSE(notif.unsolicited());
}

TEST_F(ControlServiceTest, ByeByeRequestRespondsAndTriggersCloseOnce) {
    int close_count = 0;
    service_->set_session_close_callback([&close_count] { ++close_count; });

    service_->on_message(static_cast<uint16_t>(ControlMessageType::ByeByeRequest),
                         nullptr,
                         0);
    service_->on_message(static_cast<uint16_t>(ControlMessageType::ByeByeResponse),
                         nullptr,
                         0);

    const auto sent = take_last_message();
    EXPECT_EQ(sent.message_type,
              static_cast<uint16_t>(ControlMessageType::ByeByeResponse));
    EXPECT_EQ(close_count, 1);
}
