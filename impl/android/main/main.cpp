#define LOG_TAG "AA.Engine.Main"

#include "aauto/engine/Engine.hpp"
#include "aauto/service/VideoService.hpp"
#include "aauto/service/AudioService.hpp"
#include "aauto/service/InputService.hpp"
#include "aauto/service/SensorService.hpp"
#include "aauto/service/MicrophoneService.hpp"
#include "aauto/sink/CallbackVideoSink.hpp"
#include "aauto/sink/CallbackAudioSink.hpp"
#include "aauto/utils/Logger.hpp"

#include "../transport/AndroidUsbTransport.hpp"
#include "../../common/crypto/OpenSslCryptoStrategy.hpp"
#include "../aidl/AidlEngineController.hpp"

#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>

#include <csignal>
#include <memory>
#include <thread>

using namespace aauto;

namespace {

// ===== Factory implementations =====

class AndroidTransportFactory : public engine::ITransportFactory {
public:
    std::shared_ptr<transport::ITransport>
    create(asio::any_io_executor executor,
           const std::string& descriptor) override {
        int fd = -1, ep_in = -1, ep_out = -1;

        auto parse_int = [&](const std::string& key) -> int {
            auto pos = descriptor.find(key + "=");
            if (pos == std::string::npos) return -1;
            return std::stoi(descriptor.substr(pos + key.size() + 1));
        };

        fd = parse_int("fd");
        ep_in = parse_int("ep_in");
        ep_out = parse_int("ep_out");

        if (fd < 0 || ep_in < 0 || ep_out < 0) {
            AA_LOG_E("invalid transport descriptor: %s", descriptor.c_str());
            return nullptr;
        }
        return std::make_shared<impl::AndroidUsbTransport>(
            executor, fd, ep_in, ep_out);
    }
};

class AndroidCryptoFactory : public engine::ICryptoFactory {
public:
    std::shared_ptr<crypto::ICryptoStrategy>
    create(const crypto::CryptoConfig& config) override {
        return std::make_shared<impl::OpenSslCryptoStrategy>(config);
    }
};

class AndroidServiceFactory : public engine::IServiceFactory {
public:
    using VideoDataCb = std::function<void(uint32_t session_id,
        const uint8_t* data, std::size_t size, int64_t ts, bool is_config)>;
    using AudioDataCb = std::function<void(uint32_t session_id,
        uint32_t stream_type, const uint8_t* data, std::size_t size, int64_t ts)>;

    AndroidServiceFactory(VideoDataCb video_cb, AudioDataCb audio_cb)
        : video_cb_(std::move(video_cb))
        , audio_cb_(std::move(audio_cb)) {}

    std::map<int32_t, std::shared_ptr<service::IService>>
    create_services(service::SendMessageFn send_fn) override {
        std::map<int32_t, std::shared_ptr<service::IService>> services;

        // Channel 1: Video — forward H.264 NALUs to app via callback
        service::VideoServiceConfig vcfg{800, 480, 30, 160};
        auto video_sink = std::make_shared<sink::CallbackVideoSink>(
            [this](const uint8_t* data, std::size_t size,
                   int64_t ts, bool is_config) {
                if (video_cb_) video_cb_(current_session_id_, data, size, ts, is_config);
            });
        std::vector<std::shared_ptr<sink::IVideoSink>> video_sinks;
        video_sinks.push_back(video_sink);
        services[1] = std::make_shared<service::VideoService>(
            send_fn, vcfg, std::move(video_sinks));

        // Channel 2-4: Audio — forward PCM to app via callback
        auto make_audio = [&](int ch, sink::AudioStreamType st,
                              uint32_t rate, uint32_t bits, uint32_t channels) {
            auto audio_sink = std::make_shared<sink::CallbackAudioSink>(
                [this](uint32_t stream_type, const uint8_t* data,
                       std::size_t size, int64_t ts) {
                    if (audio_cb_) audio_cb_(current_session_id_, stream_type,
                                            data, size, ts);
                });
            std::vector<std::shared_ptr<sink::IAudioSink>> sinks;
            sinks.push_back(audio_sink);
            service::AudioServiceConfig acfg{st, rate, bits, channels};
            services[ch] = std::make_shared<service::AudioService>(
                send_fn, acfg, std::move(sinks));
        };

        make_audio(2, sink::AudioStreamType::Media, 48000, 16, 2);
        make_audio(3, sink::AudioStreamType::Guidance, 16000, 16, 1);
        make_audio(4, sink::AudioStreamType::System, 16000, 16, 1);

        // Channel 5: Input source (touchscreen)
        service::InputServiceConfig icfg{800, 480};
        services[5] = std::make_shared<service::InputService>(send_fn, icfg);

        // Channel 6: Sensor source
        services[6] = std::make_shared<service::SensorService>(send_fn);

        // Channel 7: Microphone source (stub)
        services[7] = std::make_shared<service::MicrophoneService>(send_fn);

        return services;
    }

    void set_session_id(uint32_t id) { current_session_id_ = id; }

private:
    VideoDataCb video_cb_;
    AudioDataCb audio_cb_;
    uint32_t current_session_id_ = 0;
};

engine::Engine* g_engine = nullptr;

void signal_handler(int sig) {
    AA_LOG_I("received signal %d, shutting down", sig);
    if (g_engine) {
        g_engine->shutdown();
    }
}

} // anonymous namespace

int main(int /*argc*/, char* /*argv*/[]) {
    AA_LOG_I("aa-engine daemon starting");

    android::sp<android::ProcessState> ps = android::ProcessState::self();
    ps->startThreadPool();

    engine::HeadunitConfig hu_config;
    hu_config.hu_make = "Telechips";
    hu_config.hu_model = "TCC803x";
    hu_config.hu_sw_ver = "1.0.0";
    hu_config.display_name = "Android Auto";
    hu_config.video_width = 800;
    hu_config.video_height = 480;
    hu_config.video_fps = 30;
    hu_config.video_density = 160;

    // AIDL controller (created first so we can wire callbacks)
    // Will be registered as binder service below.
    impl::AidlEngineController* aidl_raw = nullptr;

    // Service factory with media callbacks → AIDL → app
    auto service_factory = std::make_shared<AndroidServiceFactory>(
        // Video callback
        [&aidl_raw](uint32_t sid, const uint8_t* data, std::size_t size,
                    int64_t ts, bool is_config) {
            if (aidl_raw) aidl_raw->on_video_data(sid, data, size, ts, is_config);
        },
        // Audio callback
        [&aidl_raw](uint32_t sid, uint32_t stream_type,
                    const uint8_t* data, std::size_t size, int64_t ts) {
            if (aidl_raw) aidl_raw->on_audio_data(sid, stream_type, data, size, ts);
        }
    );

    auto transport_factory = std::make_shared<AndroidTransportFactory>();
    auto crypto_factory = std::make_shared<AndroidCryptoFactory>();

    engine::Engine engine(hu_config, transport_factory,
                          crypto_factory, service_factory);
    g_engine = &engine;

    android::sp<impl::AidlEngineController> aidl_controller =
        new impl::AidlEngineController(&engine);
    aidl_raw = aidl_controller.get();

    android::status_t status = android::defaultServiceManager()->addService(
        android::String16("aa-engine"), aidl_controller);
    if (status != android::OK) {
        AA_LOG_E("failed to register binder service: %d", status);
        return 1;
    }
    AA_LOG_I("binder service 'aa-engine' registered");

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    AA_LOG_I("engine event loop starting (main thread)");
    engine.run(1);
    AA_LOG_I("engine event loop exited");

    g_engine = nullptr;
    aidl_raw = nullptr;
    return 0;
}
