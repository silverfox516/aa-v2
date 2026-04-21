#define LOG_TAG "AA.AidlEngine"

#include "AidlEngineController.hpp"
#include "aauto/utils/Logger.hpp"

#include <unistd.h>

namespace aauto::impl {

AidlEngineController::AidlEngineController(engine::IEngineController* engine)
    : engine_(engine) {
    engine_->register_callback(this);
}

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

    return android::binder::Status::ok();
}

android::binder::Status AidlEngineController::setSurface(
        int32_t /*sessionId*/,
        const android::sp<android::IBinder>& /*surfaceBinder*/) {
    // Surface passing not used — media decoding happens in app process.
    // Kept for AIDL compatibility; no-op.
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
        uint32_t /*session_id*/,
        const std::string& device_name,
        const std::string& /*instance_id*/) {
    AA_LOG_I("phone identified: %s", device_name.c_str());
}

void AidlEngineController::on_video_data(
        uint32_t session_id,
        const uint8_t* data, std::size_t size,
        int64_t timestamp_us, bool is_config) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (callback_ != nullptr) {
        std::vector<uint8_t> buf(data, data + size);
        callback_->onVideoData(
            static_cast<int32_t>(session_id),
            buf,
            timestamp_us,
            is_config);
    }
}

void AidlEngineController::on_audio_data(
        uint32_t session_id, uint32_t stream_type,
        const uint8_t* data, std::size_t size,
        int64_t timestamp_us) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (callback_ != nullptr) {
        std::vector<uint8_t> buf(data, data + size);
        callback_->onAudioData(
            static_cast<int32_t>(session_id),
            static_cast<int32_t>(stream_type),
            buf,
            timestamp_us);
    }
}

} // namespace aauto::impl
