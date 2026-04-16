#pragma once

#include "aauto/sink/IVideoSink.hpp"
#include "aauto/sink/IAudioSink.hpp"

#include <vector>

namespace aauto::test {

/// Records all video data passed to it for test verification.
class RecordingVideoSink : public sink::IVideoSink {
public:
    void on_configure(const sink::VideoConfig& config) override {
        last_config_ = config;
        configured_ = true;
    }

    void on_codec_config(const uint8_t* data, std::size_t size,
                         int64_t /*timestamp_us*/) override {
        codec_config_data_.assign(data, data + size);
    }

    void on_video_data(const uint8_t* data, std::size_t size,
                       int64_t /*timestamp_us*/) override {
        frame_count_++;
        total_bytes_ += size;
        last_frame_.assign(data, data + size);
    }

    void on_stop() override {
        stopped_ = true;
    }

    // Test accessors
    bool configured() const { return configured_; }
    bool stopped() const { return stopped_; }
    uint32_t frame_count() const { return frame_count_; }
    std::size_t total_bytes() const { return total_bytes_; }
    const std::vector<uint8_t>& last_frame() const { return last_frame_; }
    const std::vector<uint8_t>& codec_config_data() const { return codec_config_data_; }
    const sink::VideoConfig& last_config() const { return last_config_; }

private:
    sink::VideoConfig last_config_{};
    std::vector<uint8_t> codec_config_data_;
    std::vector<uint8_t> last_frame_;
    uint32_t frame_count_ = 0;
    std::size_t total_bytes_ = 0;
    bool configured_ = false;
    bool stopped_ = false;
};

/// Records all audio data passed to it for test verification.
class RecordingAudioSink : public sink::IAudioSink {
public:
    void on_configure(const sink::AudioConfig& config,
                      sink::AudioStreamType stream_type) override {
        last_config_ = config;
        stream_type_ = stream_type;
        configured_ = true;
    }

    void on_codec_config(const uint8_t* data, std::size_t size) override {
        codec_config_data_.assign(data, data + size);
    }

    void on_audio_data(const uint8_t* data, std::size_t size,
                       int64_t /*timestamp_us*/) override {
        frame_count_++;
        total_bytes_ += size;
        last_frame_.assign(data, data + size);
    }

    void on_stop() override {
        stopped_ = true;
    }

    bool configured() const { return configured_; }
    bool stopped() const { return stopped_; }
    uint32_t frame_count() const { return frame_count_; }
    std::size_t total_bytes() const { return total_bytes_; }
    sink::AudioStreamType stream_type() const { return stream_type_; }

private:
    sink::AudioConfig last_config_{};
    sink::AudioStreamType stream_type_{};
    std::vector<uint8_t> codec_config_data_;
    std::vector<uint8_t> last_frame_;
    uint32_t frame_count_ = 0;
    std::size_t total_bytes_ = 0;
    bool configured_ = false;
    bool stopped_ = false;
};

} // namespace aauto::test
