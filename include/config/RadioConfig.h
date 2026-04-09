#pragma once

#include <cstdint>

namespace mesh::config {

struct RadioConfig {
    float frequencyMHz    = 869.525F;
    float bandwidthKHz    = 250.0F;
    uint8_t spreadingFactor = 9;
    uint8_t codingRate      = 5;
    uint8_t syncWord        = 0x2B;
    uint16_t preambleLength = 16;
    float tcxoVoltage       = 1.6F;
    int8_t outputPowerDbm   = 22;
    float currentLimitMa    = 140.0F;
};

// Default radio preset matching Meshtastic LongFast EU
inline constexpr RadioConfig kDefaultRadioConfig{};

}  // namespace mesh::config
