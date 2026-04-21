#pragma once

#include "aauto/engine/IEngineController.hpp"

#include <com/aauto/engine/BnAAEngine.h>
#include <com/aauto/engine/IAAEngineCallback.h>

#include <binder/ParcelFileDescriptor.h>
#include <mutex>

namespace aauto::impl {

/// AIDL server: receives commands from the Java app via binder.
class AidlEngineController : public com::aauto::engine::BnAAEngine,
                             public engine::IEngineCallback {
public:
    explicit AidlEngineController(engine::IEngineController* engine);

    // IAAEngine (binder interface from app)
    android::binder::Status startSession(
        const android::os::ParcelFileDescriptor& usbFd,
        int32_t epIn, int32_t epOut,
        int32_t* _aidl_return) override;

    android::binder::Status setSurface(
        int32_t sessionId,
        const android::sp<android::IBinder>& surfaceBinder) override;

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

private:
    engine::IEngineController* engine_;
    std::mutex callback_mutex_;
    android::sp<com::aauto::engine::IAAEngineCallback> callback_;
};

} // namespace aauto::impl
