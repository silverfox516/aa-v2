#pragma once

#include "aauto/service/ServiceBase.hpp"

#include <cstdint>

namespace aauto::service {

struct MicrophoneServiceConfig {
    uint32_t sample_rate     = 16000;
    uint8_t  channels        = 1;
    uint8_t  bits_per_sample = 16;
};

/// Microphone source service (stub).
///
/// Advertises a microphone capability in ServiceDiscoveryResponse.
/// Some phones (e.g., Samsung) refuse to open channels unless the HU
/// advertises a microphone source. Actual audio capture is not yet
/// implemented — MIC_REQUEST is logged but no data is sent.
class MicrophoneService : public ServiceBase {
public:
    explicit MicrophoneService(SendMessageFn send_fn,
                               MicrophoneServiceConfig config = {})
        : ServiceBase(std::move(send_fn)), config_(config) {}

    ServiceType type() const override { return ServiceType::MediaSource; }

    void fill_config(
        aap_protobuf::service::ServiceConfiguration* config) override;

private:
    MicrophoneServiceConfig config_;
};

} // namespace aauto::service
