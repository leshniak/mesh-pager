#pragma once

/// LoRa radio parameters matching a Meshtastic channel preset.
///
/// The defaults match "LongFast EU" — the most common Meshtastic preset for
/// European ISM band (869.525 MHz). All nodes on a channel must use identical
/// radio parameters to communicate.
///
/// Key parameters and their impact:
///   - Spreading Factor (SF9): higher = longer range but slower data rate
///   - Bandwidth (250 kHz): wider = faster data rate but more noise sensitivity
///   - Coding Rate (4/5): FEC ratio — higher denominator = more error correction
///   - TX Power (22 dBm): maximum legal output for EU ISM band
///   - Sync Word (0x2B): Meshtastic-specific, distinguishes from other LoRa networks

#include <cstdint>

namespace mesh::config {

struct RadioConfig {
    float frequencyMHz    = 869.525F;    ///< EU ISM band center frequency
    float bandwidthKHz    = 250.0F;      ///< Channel bandwidth
    uint8_t spreadingFactor = 9;         ///< SF9 — compromise between range and speed
    uint8_t codingRate      = 5;         ///< 4/5 FEC coding rate
    uint8_t syncWord        = 0x2B;      ///< Meshtastic LoRa sync word
    uint16_t preambleLength = 16;        ///< Preamble symbols for synchronization
    float tcxoVoltage       = 1.6F;      ///< TCXO reference voltage (SX1262 specific)
    int8_t outputPowerDbm   = 22;        ///< TX output power (max legal for EU)
    float currentLimitMa    = 140.0F;    ///< PA over-current protection limit
};

/// Default radio preset matching Meshtastic LongFast EU.
inline constexpr RadioConfig kDefaultRadioConfig{};

}  // namespace mesh::config
