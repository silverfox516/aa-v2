#pragma once

#include "aauto/service/ServiceBase.hpp"
#include "aauto/sink/IAudioSink.hpp"

#include <memory>
#include <vector>

namespace aauto::service {

/// Receives PCM/AAC from phone for one audio stream type, multicasts to sinks.
/// Each audio stream (media, guidance, call) = separate AudioService instance.
struct AudioServiceConfig {
    sink::AudioStreamType stream_type = sink::AudioStreamType::Media;
    uint32_t sample_rate   = 48000;
    uint32_t bit_depth     = 16;
    uint32_t channel_count = 2;
};

class AudioService : public ServiceBase {
public:
    AudioService(SendMessageFn send_fn,
                 AudioServiceConfig config,
                 std::vector<std::shared_ptr<sink::IAudioSink>> sinks);

    ServiceType type() const override { return ServiceType::MediaSink; }
    void on_channel_open(uint8_t channel_id) override;
    void on_channel_close() override;
    void attach_sinks() override;
    void detach_sinks() override;
    void fill_config(aap_protobuf::service::ServiceConfiguration* config) override;

private:
    void on_setup(const uint8_t* data, std::size_t size);
    void on_config(const uint8_t* data, std::size_t size);
    void on_start(const uint8_t* data, std::size_t size);
    void on_codec_config(const uint8_t* data, std::size_t size);
    void on_data(const uint8_t* data, std::size_t size);
    void on_stop(const uint8_t* data, std::size_t size);
    void send_ack();

    AudioServiceConfig                             audio_config_;
    std::vector<std::shared_ptr<sink::IAudioSink>> sinks_;
    bool sinks_active_ = false;
    int32_t  session_id_    = 0;
    bool     started_       = false;
    sink::AudioConfig current_config_{};
};

} // namespace aauto::service
