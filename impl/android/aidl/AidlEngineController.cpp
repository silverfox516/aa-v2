#define LOG_TAG "AA.AidlEngine"

#include "AidlEngineController.hpp"
#include "aauto/utils/Logger.hpp"

#include <android/native_window.h>
#include <gui/IGraphicBufferProducer.h>
#include <gui/Surface.h>
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
        int32_t sessionId,
        const android::sp<android::IBinder>& surfaceBinder) {
    ANativeWindow* window = nullptr;
    if (surfaceBinder != nullptr) {
        auto gbp = android::interface_cast<android::IGraphicBufferProducer>(
            surfaceBinder);
        if (gbp != nullptr) {
            auto surface = new android::Surface(gbp, /*controlledByApp=*/true);
            window = static_cast<ANativeWindow*>(surface);
        }
    }

    AA_LOG_I("setSurface: session=%d window=%p", sessionId, window);
    engine_->set_video_surface(static_cast<uint32_t>(sessionId), window);

    if (window) {
        ANativeWindow_release(window);
    }
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

} // namespace aauto::impl
