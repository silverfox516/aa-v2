#define LOG_TAG "aa-engine"

#include "aauto/engine/Engine.hpp"
#include "aauto/service/VideoService.hpp"
#include "aauto/utils/Logger.hpp"

#include "../transport/AndroidUsbTransport.hpp"
#include "../sink/AMediaCodecVideoSink.hpp"
#include "../../common/crypto/OpenSslCryptoStrategy.hpp"

#include <csignal>
#include <cstdio>
#include <memory>

using namespace aauto;

namespace {

// ===== Factory implementations =====

class AndroidTransportFactory : public engine::ITransportFactory {
public:
    std::shared_ptr<transport::ITransport>
    create(asio::any_io_executor executor,
           const std::string& descriptor) override {
        // descriptor format: "usb:fd=<number>"
        int fd = -1;
        if (descriptor.find("usb:fd=") == 0) {
            fd = std::stoi(descriptor.substr(7));
        }
        if (fd < 0) {
            AA_LOG_E("invalid transport descriptor: %s", descriptor.c_str());
            return nullptr;
        }
        return std::make_shared<impl::AndroidUsbTransport>(executor, fd);
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

/// Temporary monolith entry point for Phase 1 Walking Skeleton.
/// In Phase 5, this will be replaced by a proper aa-engine daemon
/// with AIDL interface and app-layer discovery.
///
/// Usage: aa-engine <usb_fd>
///   usb_fd: file descriptor of the opened USB accessory device
int main(int argc, char* argv[]) {
    AA_LOG_I("aa-engine starting (Phase 1 monolith)");

    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <usb_fd>\n", argv[0]);
        return 1;
    }

    int usb_fd = std::atoi(argv[1]);
    if (usb_fd <= 0) {
        AA_LOG_E("invalid USB fd: %s", argv[1]);
        return 1;
    }

    // Create platform video sink
    auto video_sink = std::make_shared<impl::AMediaCodecVideoSink>();
    // TODO: set_surface() needs an ANativeWindow from the app layer.
    // For Phase 1, this will be set via a temporary mechanism.

    // Configure engine
    engine::HeadunitConfig hu_config;
    hu_config.video_width = 800;
    hu_config.video_height = 480;
    hu_config.video_fps = 30;
    hu_config.video_density = 160;
    // Cert/key: empty config = OpenSslCryptoStrategy falls back to built-in
    // reference keys (AapKeys.hpp). Override with file paths if needed:
    //   hu_config.crypto_config.cert_pem_path = "/system/etc/aa/hu_cert.pem";
    //   hu_config.crypto_config.key_pem_path = "/system/etc/aa/hu_key.pem";

    // Create factories
    auto transport_factory = std::make_shared<AndroidTransportFactory>();
    auto crypto_factory = std::make_shared<AndroidCryptoFactory>();
    auto service_factory = std::make_shared<AndroidServiceFactory>(video_sink);

    // Create engine
    engine::Engine engine(hu_config, transport_factory,
                          crypto_factory, service_factory);
    g_engine = &engine;

    // Signal handling
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Start session with the USB fd
    std::string descriptor = "usb:fd=" + std::to_string(usb_fd);
    engine.start_session(descriptor);

    // Run event loop (blocks until shutdown)
    AA_LOG_I("engine running");
    engine.run(1);

    AA_LOG_I("aa-engine exiting");
    g_engine = nullptr;
    return 0;
}
