#define LOG_TAG "AA.Engine.Main"

#include "aauto/engine/Engine.hpp"
#include "aauto/service/VideoService.hpp"
#include "aauto/utils/Logger.hpp"

#include "../transport/AndroidUsbTransport.hpp"
#include "../sink/AMediaCodecVideoSink.hpp"
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
        // descriptor format: "usb:fd=42,ep_in=129,ep_out=1"
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
    explicit AndroidServiceFactory(
        std::shared_ptr<impl::AMediaCodecVideoSink> video_sink)
        : video_sink_(std::move(video_sink)) {}

    std::map<int32_t, std::shared_ptr<service::IService>>
    create_services(service::SendMessageFn send_fn) override {
        std::map<int32_t, std::shared_ptr<service::IService>> services;

        // Video service with the platform sink
        std::vector<std::shared_ptr<sink::IVideoSink>> video_sinks;
        video_sinks.push_back(video_sink_);
        auto video_svc = std::make_shared<service::VideoService>(
            send_fn, std::move(video_sinks));
        services[1] = video_svc;  // service_id for video

        return services;
    }

private:
    std::shared_ptr<impl::AMediaCodecVideoSink> video_sink_;
};

engine::Engine* g_engine = nullptr;

void signal_handler(int sig) {
    AA_LOG_I("received signal %d, shutting down", sig);
    if (g_engine) {
        g_engine->shutdown();
    }
}

} // anonymous namespace

/// aa-engine native daemon.
///
/// Registers as a binder service ("aa-engine") and waits for the
/// Android app to connect via AIDL. The app passes USB accessory fd
/// and Surface for video output.
int main(int /*argc*/, char* /*argv*/[]) {
    AA_LOG_I("aa-engine daemon starting");

    // Initialize binder
    android::sp<android::ProcessState> ps = android::ProcessState::self();
    ps->startThreadPool();

    // Create platform video sink
    auto video_sink = std::make_shared<impl::AMediaCodecVideoSink>();

    // Configure engine
    engine::HeadunitConfig hu_config;
    hu_config.hu_make = "Telechips";
    hu_config.hu_model = "TCC803x";
    hu_config.hu_sw_ver = "1.0.0";
    hu_config.display_name = "Android Auto";
    hu_config.video_width = 800;
    hu_config.video_height = 480;
    hu_config.video_fps = 30;
    hu_config.video_density = 160;

    // Create factories
    auto transport_factory = std::make_shared<AndroidTransportFactory>();
    auto crypto_factory = std::make_shared<AndroidCryptoFactory>();
    auto service_factory = std::make_shared<AndroidServiceFactory>(video_sink);

    // Create engine
    engine::Engine engine(hu_config, transport_factory,
                          crypto_factory, service_factory);
    g_engine = &engine;

    // Create AIDL controller and register as binder service
    android::sp<impl::AidlEngineController> aidl_controller =
        new impl::AidlEngineController(&engine);

    android::status_t status = android::defaultServiceManager()->addService(
        android::String16("aa-engine"), aidl_controller);
    if (status != android::OK) {
        AA_LOG_E("failed to register binder service: %d", status);
        return 1;
    }
    AA_LOG_I("binder service 'aa-engine' registered");

    // Signal handling
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Binder thread pool runs in background threads (started by startThreadPool above).
    // Main thread runs the engine event loop — blocks until engine.shutdown().
    // Signal handler calls engine.shutdown() which unblocks run().
    AA_LOG_I("engine event loop starting (main thread)");
    engine.run(1);
    AA_LOG_I("engine event loop exited");

    AA_LOG_I("aa-engine daemon exiting");
    g_engine = nullptr;
    return 0;
}
