#pragma once

#include "aauto/service/ServiceBase.hpp"
#include "aauto/sink/IVideoSink.hpp"

#include <functional>
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
    /// Callback when phone requests video focus change (e.g., "exit" button)
    using VideoFocusCallback = std::function<void(bool projected)>;
    VideoService(SendMessageFn send_fn,
                 VideoServiceConfig config,
                 std::vector<std::shared_ptr<sink::IVideoSink>> sinks);

    void set_video_focus_callback(VideoFocusCallback cb) {
        focus_callback_ = std::move(cb);
    }

    void set_video_focus(bool projected) override;
    void attach_sinks() override;
    void detach_sinks() override;
    void send_video_focus(bool gain);

    ServiceType type() const override { return ServiceType::MediaSink; }
    void on_channel_open(uint8_t channel_id) override;
    void on_channel_close() override;
    void set_native_window(void* window) override;
    void fill_config(aap_protobuf::service::ServiceConfiguration* config) override;

private:
    void on_setup(const uint8_t* data, std::size_t size);
    void on_start(const uint8_t* data, std::size_t size);
    void on_codec_config(const uint8_t* data, std::size_t size);
    void on_data(const uint8_t* data, std::size_t size);
    void send_ack();

    VideoFocusCallback focus_callback_;
    VideoServiceConfig video_config_;
    std::vector<std::shared_ptr<sink::IVideoSink>> sinks_;
    bool sinks_active_ = false;
    int32_t  session_id_    = 0;
    bool     started_       = false;
    sink::VideoConfig current_config_{};
};

} // namespace aauto::service
