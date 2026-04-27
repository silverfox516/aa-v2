#define LOG_TAG "AA.Engine.Main"

#include "aauto/engine/Engine.hpp"
#include "aauto/service/VideoService.hpp"
#include "aauto/service/AudioService.hpp"
#include "aauto/service/InputService.hpp"
#include "aauto/service/SensorService.hpp"
#include "aauto/service/MicrophoneService.hpp"
#include "aauto/service/NavigationStatusService.hpp"
#include "aauto/service/PhoneStatusService.hpp"
#include "aauto/service/MediaPlaybackService.hpp"
#include "aauto/service/GenericNotificationService.hpp"
#include "aauto/service/MediaBrowserService.hpp"
#include "aauto/service/BluetoothService.hpp"
#include "aauto/service/VendorExtensionService.hpp"
#include "aauto/sink/CallbackVideoSink.hpp"
#include "aauto/sink/CallbackAudioSink.hpp"
#include "aauto/utils/Logger.hpp"

#include "../transport/AndroidUsbTransport.hpp"
#include "../transport/AndroidTcpTransport.hpp"
#include "../../common/crypto/OpenSslCryptoStrategy.hpp"
#include "../aidl/AidlEngineController.hpp"

#include "aauto/transport/BufferedTransport.hpp"

#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>

#include <android/log.h>
#include <csignal>
#include <memory>
#include <thread>

using namespace aauto;

namespace {

void android_log_function(LogLevel level, const char* tag,
                          const char* fmt, va_list args) {
    int prio;
    switch (level) {
        case LogLevel::Debug: prio = ANDROID_LOG_DEBUG; break;
        case LogLevel::Info:  prio = ANDROID_LOG_INFO;  break;
        case LogLevel::Warn:  prio = ANDROID_LOG_WARN;  break;
        case LogLevel::Error: prio = ANDROID_LOG_ERROR; break;
        default:              prio = ANDROID_LOG_INFO;   break;
    }
    // Pad TAG to fixed width + prepend session tag for aligned output
    char padded_tag[24];
    snprintf(padded_tag, sizeof(padded_tag), "%-18s", tag);

    std::string session_tag = aauto::get_session_tag();
    if (session_tag.empty()) {
        __android_log_vprint(prio, padded_tag, fmt, args);
    } else {
        char buf[4096];
        vsnprintf(buf, sizeof(buf), fmt, args);
        __android_log_print(prio, padded_tag, "%-14s %s", session_tag.c_str(), buf);
    }
}

} // anonymous namespace

// ===== Factory implementations =====

namespace {

class AndroidTransportFactory : public engine::ITransportFactory {
public:
    std::shared_ptr<transport::ITransport>
    create(asio::any_io_executor executor,
           const std::string& descriptor) override {

        auto parse_int = [&](const std::string& key) -> int {
            auto pos = descriptor.find(key + "=");
            if (pos == std::string::npos) return -1;
            return std::stoi(descriptor.substr(pos + key.size() + 1));
        };

        std::shared_ptr<transport::ITransport> underlying;
        if (descriptor.find("usb:") == 0) {
            int fd = parse_int("fd");
            int ep_in = parse_int("ep_in");
            int ep_out = parse_int("ep_out");
            if (fd < 0 || ep_in < 0 || ep_out < 0) {
                AA_LOG_E("invalid USB descriptor: %s", descriptor.c_str());
                return nullptr;
            }
            underlying = std::make_shared<impl::AndroidUsbTransport>(
                executor, fd, ep_in, ep_out);
        } else if (descriptor.find("tcp:") == 0) {
            int fd = parse_int("fd");
            if (fd < 0) {
                AA_LOG_E("invalid TCP descriptor: %s", descriptor.c_str());
                return nullptr;
            }
            underlying = std::make_shared<impl::AndroidTcpTransport>(
                executor, fd);
        } else {
            AA_LOG_E("unknown transport descriptor: %s", descriptor.c_str());
            return nullptr;
        }

        // Wrap in BufferedTransport so the underlying transport keeps
        // reading regardless of upper-layer processing speed (see
        // BufferedTransport.hpp). The wrap is done here, in the platform
        // factory, so every transport implementation gets the behavior
        // for free without per-platform duplication.
        auto buffered =
            std::make_shared<transport::BufferedTransport>(std::move(underlying));
        buffered->start();
        return buffered;
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
    using VideoFocusCb = std::function<void(uint32_t session_id, bool projected)>;

    AndroidServiceFactory(const engine::HeadunitConfig& config,
                          VideoDataCb video_cb, AudioDataCb audio_cb,
                          VideoFocusCb focus_cb)
        : hu_(config)
        , video_cb_(std::move(video_cb))
        , audio_cb_(std::move(audio_cb))
        , focus_cb_(std::move(focus_cb)) {}

    std::map<int32_t, std::shared_ptr<service::IService>>
    create_services(service::SendMessageFn send_fn) override {
        std::map<int32_t, std::shared_ptr<service::IService>> services;

        // Channel 1: Video — forward H.264 NALUs to app via AIDL callback
        service::VideoServiceConfig vcfg{
            hu_.video_width, hu_.video_height, hu_.video_fps, hu_.video_density};
        uint32_t sid = current_session_id_;
        auto video_sink = std::make_shared<sink::CallbackVideoSink>(
            [this, sid](const uint8_t* data, std::size_t size,
                   int64_t ts, bool is_config) {
                if (video_cb_) video_cb_(sid, data, size, ts, is_config);
            });
        std::vector<std::shared_ptr<sink::IVideoSink>> video_sinks;
        video_sinks.push_back(video_sink);
        auto video_svc = std::make_shared<service::VideoService>(
            send_fn, vcfg, std::move(video_sinks));
        video_svc->set_video_focus_callback(
            [this, sid](bool projected) {
                if (focus_cb_) focus_cb_(sid, projected);
            });
        services[1] = video_svc;

        // Channel 2-4: Audio — forward PCM to app via callback
        auto make_audio = [&](int ch, sink::AudioStreamType st,
                              uint32_t rate, uint32_t bits, uint32_t channels) {
            auto audio_sink = std::make_shared<sink::CallbackAudioSink>(
                [this, sid](uint32_t stream_type, const uint8_t* data,
                       std::size_t size, int64_t ts) {
                    if (audio_cb_) audio_cb_(sid, stream_type,
                                            data, size, ts);
                });
            std::vector<std::shared_ptr<sink::IAudioSink>> sinks;
            sinks.push_back(audio_sink);
            service::AudioServiceConfig acfg{st, rate, bits, channels};
            services[ch] = std::make_shared<service::AudioService>(
                send_fn, acfg, std::move(sinks));
        };

        make_audio(2, sink::AudioStreamType::Media,
                   hu_.audio_sample_rate, hu_.audio_bit_depth, hu_.audio_channels);
        make_audio(3, sink::AudioStreamType::Guidance, 16000, 16, 1);
        make_audio(4, sink::AudioStreamType::System, 16000, 16, 1);

        // Channel 5: Input source (touchscreen)
        service::InputServiceConfig icfg{hu_.video_width, hu_.video_height};
        services[5] = std::make_shared<service::InputService>(send_fn, icfg);

        // Channel 6: Sensor source
        services[6] = std::make_shared<service::SensorService>(send_fn);

        // Channel 7: Microphone source (stub)
        services[7] = std::make_shared<service::MicrophoneService>(send_fn);

        // Channel 10: Media playback status — handlers parse and log
        // PLAYBACK_STATUS / PLAYBACK_METADATA. Re-enabled 2026-04-27
        // (was unregistered in troubleshooting #22 because its
        // fill_config-only state was throttling video cadence).
        services[10] = std::make_shared<service::MediaPlaybackService>(send_fn);

        // Channels 8 / 9 / 11 / 12 / 13 / 14 (Nav / PhoneStatus /
        // Notification / MediaBrowser / Bluetooth / VendorExtension)
        // remain unregistered. Their service classes are still in-tree
        // (fill_config documents the proto layout) — re-register here
        // with real response handlers when each channel becomes a
        // learning target. See architecture_review.md G.0 for the
        // policy and G.1 / G.2 / G.3 / G.3a for per-channel reasons.

        return services;
    }

    void set_session_id(uint32_t id) override { current_session_id_ = id; }

private:
    engine::HeadunitConfig hu_;
    VideoDataCb video_cb_;
    AudioDataCb audio_cb_;
    VideoFocusCb focus_cb_;
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
    set_log_function(android_log_function);
    AA_LOG_I("aa-engine daemon starting");

    android::sp<android::ProcessState> ps = android::ProcessState::self();
    ps->startThreadPool();

    engine::HeadunitConfig hu_config;
    hu_config.hu_make = "Telechips";
    hu_config.hu_model = "TCC803x";
    hu_config.hu_sw_ver = "1.0.0";
    hu_config.display_name = "Android Auto";
    hu_config.video_width = 1280;
    hu_config.video_height = 720;
    hu_config.video_fps = 30;
    hu_config.video_density = 160;

    // AIDL controller (created first so we can wire callbacks)
    // Will be registered as binder service below.
    impl::AidlEngineController* aidl_raw = nullptr;

    // Service factory with media callbacks → AIDL → app
    auto service_factory = std::make_shared<AndroidServiceFactory>(
        hu_config,
        // Video callback
        [&aidl_raw](uint32_t sid, const uint8_t* data, std::size_t size,
                    int64_t ts, bool is_config) {
            if (aidl_raw) aidl_raw->on_video_data(sid, data, size, ts, is_config);
        },
        // Audio callback
        [&aidl_raw](uint32_t sid, uint32_t stream_type,
                    const uint8_t* data, std::size_t size, int64_t ts) {
            if (aidl_raw) aidl_raw->on_audio_data(sid, stream_type, data, size, ts);
        },
        // Video focus callback
        [&aidl_raw](uint32_t sid, bool projected) {
            if (aidl_raw) aidl_raw->on_video_focus_changed(sid, projected);
        }
    );

    auto transport_factory = std::make_shared<AndroidTransportFactory>();
    auto crypto_factory = std::make_shared<AndroidCryptoFactory>();

    engine::Engine engine(hu_config, transport_factory,
                          crypto_factory, service_factory);
    g_engine = &engine;

    android::sp<impl::AidlEngineController> aidl_controller =
        new impl::AidlEngineController(&engine, hu_config);
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
