#pragma once

#include "aauto/service/ServiceBase.hpp"
#include "aauto/sink/IAudioSink.hpp"

#include <memory>
#include <vector>

namespace aauto::service {

/// Receives PCM/AAC from phone for one audio stream type, multicasts to sinks.
/// Each audio stream (media, guidance, call) = separate AudioService instance.
class AudioService : public ServiceBase {
public:
    AudioService(SendMessageFn send_fn,
                 sink::AudioStreamType stream_type,
                 std::vector<std::shared_ptr<sink::IAudioSink>> sinks);

    ServiceType type() const override { return ServiceType::MediaSink; }
    void on_channel_open(uint8_t channel_id) override;
    void on_channel_close() override;

private:
    void on_setup(const uint8_t* data, std::size_t size);
    void on_config(const uint8_t* data, std::size_t size);
    void on_start(const uint8_t* data, std::size_t size);
    void on_codec_config(const uint8_t* data, std::size_t size);
    void on_data(const uint8_t* data, std::size_t size);
    void on_stop(const uint8_t* data, std::size_t size);
    void send_ack();

    sink::AudioStreamType                          stream_type_;
    std::vector<std::shared_ptr<sink::IAudioSink>> sinks_;
    uint32_t max_unacked_   = 1;
    uint32_t unacked_count_ = 0;
    int32_t  session_id_    = 0;
    bool     started_       = false;
    sink::AudioConfig current_config_{};
};

} // namespace aauto::service
