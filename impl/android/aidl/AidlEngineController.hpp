#pragma once

#include "aauto/engine/HeadunitConfig.hpp"
#include "aauto/engine/IEngineController.hpp"

#include <com/aauto/engine/BnAAEngine.h>
#include <com/aauto/engine/IAAEngineCallback.h>

#include <binder/ParcelFileDescriptor.h>
#include <mutex>
#include <vector>

namespace aauto::impl {

/// AIDL server: receives commands from the Java app via binder.
///
/// Media data (video/audio) is queued and sent on a dedicated thread
/// to avoid blocking the engine's asio strand during Binder transfers.
class AidlEngineController : public com::aauto::engine::BnAAEngine,
                             public engine::IEngineCallback {
public:
    AidlEngineController(engine::IEngineController* engine,
                         const engine::HeadunitConfig& config);
    ~AidlEngineController();

    // IAAEngine (binder interface from app)
    android::binder::Status startSession(
        const android::os::ParcelFileDescriptor& usbFd,
        int32_t epIn, int32_t epOut,
        int32_t* _aidl_return) override;

    android::binder::Status startTcpSession(
        int32_t port,
        int32_t* _aidl_return) override;

    android::binder::Status sendTouchEvent(
        int32_t sessionId, int32_t x, int32_t y, int32_t action) override;

    android::binder::Status stopSession(int32_t sessionId) override;
    android::binder::Status stopAll() override;

    android::binder::Status registerCallback(
        const android::sp<com::aauto::engine::IAAEngineCallback>& callback) override;

    // IEngineCallback (events from engine)
    void on_session_state_changed(uint32_t session_id,
                                  engine::SessionStatus status) override;
    void on_session_error(uint32_t session_id,
                          const std::error_code& ec,
                          const std::string& detail) override;
    void on_phone_identified(uint32_t session_id,
                             const std::string& device_name,
                             const std::string& instance_id) override;
    void on_video_data(uint32_t session_id,
                       const uint8_t* data, std::size_t size,
                       int64_t timestamp_us, bool is_config) override;
    void on_audio_data(uint32_t session_id, uint32_t stream_type,
                       const uint8_t* data, std::size_t size,
                       int64_t timestamp_us) override;

private:
    engine::IEngineController* engine_;
    engine::HeadunitConfig hu_config_;

    std::mutex callback_mutex_;
    android::sp<com::aauto::engine::IAAEngineCallback> callback_;
};

} // namespace aauto::impl
