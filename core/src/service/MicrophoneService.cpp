#define LOG_TAG "AA.MicrophoneService"

#include "aauto/service/MicrophoneService.hpp"
#include "aauto/utils/Logger.hpp"

#include <aap_protobuf/service/Service.pb.h>
#include <aap_protobuf/service/media/source/MediaSourceService.pb.h>
#include <aap_protobuf/service/media/shared/message/MediaCodecType.pb.h>
#include <aap_protobuf/service/media/shared/message/AudioConfiguration.pb.h>

namespace aauto::service {

void MicrophoneService::fill_config(
        aap_protobuf::service::ServiceConfiguration* config) {
    auto* source = config->mutable_media_source_service();
    source->set_available_type(
        aap_protobuf::service::media::shared::message::MEDIA_CODEC_AUDIO_PCM);

    auto* audio_cfg = source->mutable_audio_config();
    audio_cfg->set_sampling_rate(config_.sample_rate);
    audio_cfg->set_number_of_bits(config_.bits_per_sample);
    audio_cfg->set_number_of_channels(config_.channels);
}

} // namespace aauto::service
