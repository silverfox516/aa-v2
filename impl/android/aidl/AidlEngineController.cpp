#define LOG_TAG "AA.AidlEngine"

#include "AidlEngineController.hpp"
#include "aauto/utils/Logger.hpp"

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>

namespace aauto::impl {

AidlEngineController::AidlEngineController(engine::IEngineController* engine,
                                           const engine::HeadunitConfig& config)
    : engine_(engine)
    , hu_config_(config) {
    engine_->register_callback(this);
}

AidlEngineController::~AidlEngineController() = default;

// ===== IAAEngine (binder calls from app) =====

android::binder::Status AidlEngineController::startSession(
        const android::os::ParcelFileDescriptor& usbFd,
        int32_t epIn, int32_t epOut,
        int32_t* _aidl_return) {
    int fd = dup(usbFd.get());
    if (fd < 0) {
        AA_LOG_E("failed to dup USB fd");
        *_aidl_return = -1;
        return android::binder::Status::ok();
    }

    AA_LOG_I("startSession: fd=%d ep_in=0x%02x ep_out=0x%02x", fd, epIn, epOut);

    std::string descriptor = "usb:fd=" + std::to_string(fd) +
                             ",ep_in=" + std::to_string(epIn) +
                             ",ep_out=" + std::to_string(epOut);
    uint32_t sid = engine_->start_session(descriptor);
    if (sid == 0) {
        ::close(fd);
        AA_LOG_E("engine failed to start session");
        *_aidl_return = -1;
        return android::binder::Status::ok();
    }
    *_aidl_return = static_cast<int32_t>(sid);

    // Send display config to app so it knows video dimensions
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (callback_ != nullptr) {
            callback_->onSessionConfig(
                static_cast<int32_t>(sid),
                static_cast<int32_t>(hu_config_.video_width),
                static_cast<int32_t>(hu_config_.video_height));
        }
    }

    return android::binder::Status::ok();
}


android::binder::Status AidlEngineController::startTcpSession(
        int32_t port,
        int32_t* _aidl_return) {
    AA_LOG_I("startTcpSession: port=%d", port);

    // Accept TCP connection on Binder thread (not io_context) to avoid
    // blocking the engine event loop while waiting for phone WiFi connect.
    int accepted_fd = -1;
    {
        int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            AA_LOG_E("TCP socket() failed: %s", strerror(errno));
            *_aidl_return = -1;
            return android::binder::Status::ok();
        }
        int opt = 1;
        ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        if (::bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            AA_LOG_E("TCP bind(%d) failed: %s", port, strerror(errno));
            ::close(server_fd);
            *_aidl_return = -1;
            return android::binder::Status::ok();
        }
        ::listen(server_fd, 1);
        AA_LOG_I("TCP listening on port %d, waiting for phone...", port);

        accepted_fd = ::accept(server_fd, nullptr, nullptr);
        ::close(server_fd);
        if (accepted_fd < 0) {
            AA_LOG_E("TCP accept failed: %s", strerror(errno));
            *_aidl_return = -1;
            return android::binder::Status::ok();
        }
        AA_LOG_I("TCP accepted: fd=%d", accepted_fd);
    }

    std::string descriptor = "tcp:fd=" + std::to_string(accepted_fd);
    uint32_t sid = engine_->start_session(descriptor);
    if (sid == 0) {
        ::close(accepted_fd);
        AA_LOG_E("engine failed to start TCP session");
        *_aidl_return = -1;
        return android::binder::Status::ok();
    }
    *_aidl_return = static_cast<int32_t>(sid);

    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (callback_ != nullptr) {
            callback_->onSessionConfig(
                static_cast<int32_t>(sid),
                static_cast<int32_t>(hu_config_.video_width),
                static_cast<int32_t>(hu_config_.video_height));
        }
    }

    return android::binder::Status::ok();
}

android::binder::Status AidlEngineController::sendTouchEvent(
        int32_t sessionId, int32_t x, int32_t y, int32_t action) {
    engine_->send_touch_event(static_cast<uint32_t>(sessionId), x, y, action);
    return android::binder::Status::ok();
}

android::binder::Status AidlEngineController::sendMediaKey(
        int32_t sessionId, int32_t keycode) {
    engine_->send_media_key(static_cast<uint32_t>(sessionId), keycode);
    return android::binder::Status::ok();
}

android::binder::Status AidlEngineController::releaseAudioFocus(int32_t sessionId) {
    engine_->release_audio_focus(static_cast<uint32_t>(sessionId));
    return android::binder::Status::ok();
}

android::binder::Status AidlEngineController::gainAudioFocus(int32_t sessionId) {
    engine_->gain_audio_focus(static_cast<uint32_t>(sessionId));
    return android::binder::Status::ok();
}

android::binder::Status AidlEngineController::setVideoFocus(
        int32_t sessionId, bool projected) {
    AA_LOG_I("setVideoFocus: session=%d projected=%d", sessionId, projected);
    engine_->set_video_focus(static_cast<uint32_t>(sessionId), projected);
    return android::binder::Status::ok();
}

android::binder::Status AidlEngineController::attachAllSinks(int32_t sessionId) {
    engine_->attach_all_sinks(static_cast<uint32_t>(sessionId));
    return android::binder::Status::ok();
}

android::binder::Status AidlEngineController::detachAllSinks(int32_t sessionId) {
    engine_->detach_all_sinks(static_cast<uint32_t>(sessionId));
    return android::binder::Status::ok();
}

android::binder::Status AidlEngineController::stopSession(int32_t sessionId) {
    AA_LOG_I("stopSession: id=%d", sessionId);
    engine_->stop_session(static_cast<uint32_t>(sessionId));
    return android::binder::Status::ok();
}

android::binder::Status AidlEngineController::stopAll() {
    AA_LOG_I("stopAll");
    engine_->stop_all();
    return android::binder::Status::ok();
}

android::binder::Status AidlEngineController::registerCallback(
        const android::sp<com::aauto::engine::IAAEngineCallback>& callback) {
    AA_LOG_I("registerCallback");
    std::lock_guard<std::mutex> lock(callback_mutex_);
    callback_ = callback;
    return android::binder::Status::ok();
}

// ===== IEngineCallback (events from engine -> app) =====

void AidlEngineController::on_session_state_changed(
        uint32_t session_id, engine::SessionStatus status) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (callback_ != nullptr) {
        callback_->onSessionStateChanged(
            static_cast<int32_t>(session_id),
            static_cast<int32_t>(status));
    }
}

void AidlEngineController::on_session_error(
        uint32_t session_id,
        const std::error_code& ec,
        const std::string& detail) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (callback_ != nullptr) {
        callback_->onSessionError(
            static_cast<int32_t>(session_id),
            ec.value(),
            android::String16(detail.c_str()));
    }
}

void AidlEngineController::on_phone_identified(
        uint32_t session_id,
        const std::string& device_name,
        const std::string& /*instance_id*/) {
    AA_LOG_I("phone identified: session=%u name=%s",
             session_id, device_name.c_str());
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (callback_ != nullptr) {
        callback_->onPhoneIdentified(
            static_cast<int32_t>(session_id),
            android::String16(device_name.c_str()));
    }
}

// ===== Media callbacks — direct Binder call (oneway, non-blocking) =====
// Called on the asio strand. oneway AIDL methods return immediately
// without waiting for the app to process, so this does not block the strand.

void AidlEngineController::on_video_data(
        uint32_t session_id,
        const uint8_t* data, std::size_t size,
        int64_t timestamp_us, bool is_config) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (callback_ == nullptr) return;
    std::vector<uint8_t> vec(data, data + size);
    callback_->onVideoData(
        static_cast<int32_t>(session_id), vec,
        timestamp_us, is_config);
}

void AidlEngineController::on_audio_data(
        uint32_t session_id, uint32_t stream_type,
        const uint8_t* data, std::size_t size,
        int64_t timestamp_us) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (callback_ == nullptr) return;
    std::vector<uint8_t> vec(data, data + size);
    callback_->onAudioData(
        static_cast<int32_t>(session_id),
        static_cast<int32_t>(stream_type),
        vec, timestamp_us);
}


void AidlEngineController::on_video_focus_changed(
        uint32_t session_id, bool projected) {
    AA_LOG_I("video focus changed: session=%u projected=%d",
             session_id, projected);
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (callback_ != nullptr) {
        callback_->onVideoFocusChanged(
            static_cast<int32_t>(session_id), projected);
    }
}

void AidlEngineController::on_playback_status(
        uint32_t session_id,
        int32_t state,
        const std::string& media_source,
        uint32_t playback_seconds,
        bool shuffle,
        bool repeat,
        bool repeat_one) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (callback_ == nullptr) return;
    callback_->onPlaybackStatus(
        static_cast<int32_t>(session_id),
        state,
        android::String16(media_source.c_str()),
        static_cast<int32_t>(playback_seconds),
        shuffle, repeat, repeat_one);
}

void AidlEngineController::on_playback_metadata(
        uint32_t session_id,
        const std::string& song,
        const std::string& artist,
        const std::string& album,
        const std::vector<uint8_t>& album_art,
        uint32_t duration_seconds) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (callback_ == nullptr) return;
    callback_->onPlaybackMetadata(
        static_cast<int32_t>(session_id),
        android::String16(song.c_str()),
        android::String16(artist.c_str()),
        android::String16(album.c_str()),
        album_art,
        static_cast<int32_t>(duration_seconds));
}

} // namespace aauto::impl
