#define LOG_TAG "AA.AidlEngine"

#include "AidlEngineController.hpp"
#include "aauto/utils/Logger.hpp"

#include <unistd.h>

namespace aauto::impl {

AidlEngineController::AidlEngineController(engine::IEngineController* engine,
                                           const engine::HeadunitConfig& config)
    : engine_(engine)
    , hu_config_(config)
    , media_thread_(&AidlEngineController::media_sender_loop, this) {
    engine_->register_callback(this);
}

AidlEngineController::~AidlEngineController() {
    media_running_ = false;
    media_cv_.notify_all();
    if (media_thread_.joinable()) media_thread_.join();
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

android::binder::Status AidlEngineController::setSurface(
        int32_t /*sessionId*/,
        const android::sp<android::IBinder>& /*surfaceBinder*/) {
    // No-op: media decoding happens in app process (F.12).
    return android::binder::Status::ok();
}

android::binder::Status AidlEngineController::sendTouchEvent(
        int32_t sessionId, int32_t x, int32_t y, int32_t action) {
    engine_->send_touch_event(static_cast<uint32_t>(sessionId), x, y, action);
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

// ===== Media callbacks — push to queue, return immediately =====
// These are called on the asio strand. Must not block.

void AidlEngineController::on_video_data(
        uint32_t session_id,
        const uint8_t* data, std::size_t size,
        int64_t timestamp_us, bool is_config) {
    std::lock_guard<std::mutex> lock(media_mutex_);
    if (media_queue_.size() >= kMaxMediaQueueSize) return;
    media_queue_.push_back(MediaItem{
        MediaItem::Video,
        static_cast<int32_t>(session_id),
        std::vector<uint8_t>(data, data + size),
        timestamp_us, is_config, 0
    });
    media_cv_.notify_one();
}

void AidlEngineController::on_audio_data(
        uint32_t session_id, uint32_t stream_type,
        const uint8_t* data, std::size_t size,
        int64_t timestamp_us) {
    std::lock_guard<std::mutex> lock(media_mutex_);
    if (media_queue_.size() >= kMaxMediaQueueSize) {
        return;
    }
    media_queue_.push_back(MediaItem{
        MediaItem::Audio,
        static_cast<int32_t>(session_id),
        std::vector<uint8_t>(data, data + size),
        timestamp_us,
        false,
        static_cast<int32_t>(stream_type)
    });
    media_cv_.notify_one();
}

// ===== Media sender thread — audio via Binder (video uses pipe directly) =====

void AidlEngineController::media_sender_loop() {
    AA_LOG_I("media sender thread started");

    while (media_running_) {
        std::deque<MediaItem> batch;
        {
            std::unique_lock<std::mutex> lock(media_mutex_);
            media_cv_.wait(lock, [this] {
                return !media_queue_.empty() || !media_running_;
            });
            if (!media_running_ && media_queue_.empty()) break;
            batch.swap(media_queue_);
        }

        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (callback_ == nullptr) continue;

        for (auto& item : batch) {
            if (item.type == MediaItem::Video) {
                callback_->onVideoData(
                    item.session_id, item.data,
                    item.timestamp_us, item.is_config);
            } else {
                callback_->onAudioData(
                    item.session_id, item.stream_type,
                    item.data, item.timestamp_us);
            }
        }
    }

    AA_LOG_I("media sender thread exited");
}

} // namespace aauto::impl
