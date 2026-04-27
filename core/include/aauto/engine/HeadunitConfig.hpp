#pragma once

#include "aauto/crypto/ICryptoStrategy.hpp"

#include <cstdint>
#include <string>

namespace aauto::engine {

struct HeadunitConfig {
    std::string hu_make      = "TCC";
    std::string hu_model     = "TCC803x";
    std::string hu_sw_ver    = "1.0.0";
    std::string display_name = "Android Auto";

    uint32_t video_width   = 800;
    uint32_t video_height  = 480;
    uint32_t video_fps     = 30;
    uint32_t video_density = 160;

    uint32_t audio_sample_rate = 48000;
    uint32_t audio_bit_depth   = 16;
    uint32_t audio_channels    = 2;

    // Head unit Bluetooth MAC address advertised via BluetoothService
    // capability. Placeholder value; should be overridden at runtime
    // (e.g., from BluetoothAdapter.getAddress() via AIDL) when real
    // BT pairing is wired up.
    std::string bluetooth_mac = "02:00:00:00:00:00";

    crypto::CryptoConfig crypto_config;
};

} // namespace aauto::engine
