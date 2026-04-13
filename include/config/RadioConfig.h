#pragma once

/// LoRa radio parameters matching a Meshtastic channel preset.
///
/// The active config (`kDefaultRadioConfig`) must match the preset used by
/// all other nodes on the channel. Common EU presets:
///
///   Preset       SF   BW(kHz)  CR   Data rate   Link budget
///   ─────────────────────────────────────────────────────────
///   LongFast     11   250      4/5  1.07 kbps   153 dB
///   MediumFast    9   250      4/5  3.52 kbps   148 dB
///
/// Both use 869.525 MHz (EU ISM), sync word 0x2B, preamble 16.
/// The only difference is spreading factor — higher SF = longer range,
/// slower data rate.
///
/// To switch presets, change `spreadingFactor` in the active config.

#include <cstdint>

namespace mesh::config {

struct RadioConfig {
    float frequencyMHz    = 869.525F;    ///< EU ISM band center frequency
    float bandwidthKHz    = 250.0F;      ///< Channel bandwidth
    uint8_t spreadingFactor = 9;         ///< SF9 = MediumFast, SF11 = LongFast
    uint8_t codingRate      = 5;         ///< 4/5 FEC coding rate
    uint8_t syncWord        = 0x2B;      ///< Meshtastic LoRa sync word
    uint16_t preambleLength = 16;        ///< Preamble symbols for synchronization
    float tcxoVoltage       = 1.6F;      ///< TCXO reference voltage (SX1262 specific)
    int8_t outputPowerDbm   = 22;        ///< TX output power (max legal for EU)
    float currentLimitMa    = 140.0F;    ///< PA over-current protection limit
};

/// Active radio preset — MediumFast EU (SF9, BW250, 869.525 MHz).
inline constexpr RadioConfig kDefaultRadioConfig{};

/// LongFast EU preset — longer range, slower data rate (SF11, BW250).
/// To use: change kDefaultRadioConfig or assign this to the radio.
inline constexpr RadioConfig kLongFastEuConfig{
    .frequencyMHz    = 869.525F,
    .bandwidthKHz    = 250.0F,
    .spreadingFactor = 11,
    .codingRate      = 5,
    .syncWord        = 0x2B,
    .preambleLength  = 16,
    .tcxoVoltage     = 1.6F,
    .outputPowerDbm  = 22,
    .currentLimitMa  = 140.0F,
};

}  // namespace mesh::config
