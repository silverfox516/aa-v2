#pragma once

#include "aauto/service/ServiceBase.hpp"
#include "aauto/sink/IVideoSink.hpp"

#include <memory>
#include <vector>

namespace aauto::service {

/// Receives H.264/VP9/H265 data from phone, multicasts to video sinks.
///
/// Flow: SETUP -> CONFIG -> START -> [CODEC_CONFIG] -> DATA* -> STOP
/// Flow control: phone sends max_unacked in CONFIG. HU sends ACK after N frames.
struct VideoServiceConfig {
    uint32_t width   = 800;
    uint32_t height  = 480;
    uint32_t fps     = 30;
    uint32_t density = 160;
};

class VideoService : public ServiceBase {
public:
    VideoService(SendMessageFn send_fn,
                 VideoServiceConfig config,
                 std::vector<std::shared_ptr<sink::IVideoSink>> sinks);

    ServiceType type() const override { return ServiceType::MediaSink; }
    void on_channel_open(uint8_t channel_id) override;
    void on_channel_close() override;
    void fill_config(aap_protobuf::service::ServiceConfiguration* config) override;

private:
    void on_setup(const uint8_t* data, std::size_t size);
    void on_start(const uint8_t* data, std::size_t size);
    void on_codec_config(const uint8_t* data, std::size_t size);
    void on_data(const uint8_t* data, std::size_t size);
    void send_ack();
    void send_video_focus(bool gain);

    VideoServiceConfig video_config_;
    std::vector<std::shared_ptr<sink::IVideoSink>> sinks_;
    uint32_t max_unacked_   = 5;
    uint32_t unacked_count_ = 0;
    int32_t  session_id_    = 0;
    bool     started_       = false;
    sink::VideoConfig current_config_{};
};

} // namespace aauto::service
