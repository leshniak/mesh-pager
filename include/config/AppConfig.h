#pragma once

#include <cstdint>

namespace mesh::config {

// Power management
inline constexpr uint16_t kChargeCurrent     = 256;   // mA
inline constexpr uint16_t kChargeVoltage     = 4200;  // mV
inline constexpr uint32_t kSleepTimeoutMs    = 15000;
inline constexpr uint32_t kDebounceGuardMs   = 1000;
inline constexpr uint16_t kButtonHoldMs      = 1000;

// Buzzer tones (frequency Hz, duration ms)
inline constexpr uint16_t kTxToneHz          = 4000;
inline constexpr uint16_t kRxToneHz          = 6000;
inline constexpr uint16_t kToneDurationMs    = 50;

inline constexpr uint16_t kSleepToneHz       = 2500;
inline constexpr uint16_t kSleepToneDurMs    = 100;
inline constexpr uint16_t kSleepToneGapMs    = 150;

inline constexpr uint16_t kPowerOffTone1Hz   = 6000;
inline constexpr uint16_t kPowerOffTone2Hz   = 4000;
inline constexpr uint16_t kPowerOffTone3Hz   = 2500;
inline constexpr uint16_t kPowerOffToneDurMs = 100;
inline constexpr uint16_t kPowerOffToneGapMs = 150;

// Mesh flags
inline constexpr uint8_t kHopLimit  = 3;
inline constexpr uint8_t kHopStart  = 3;
inline constexpr bool kWantAck      = false;
inline constexpr bool kViaMqtt      = false;

// Serial
inline constexpr uint32_t kSerialBaudRate = 115200;

// Preferences
inline constexpr const char* kAppName       = "mesh-remote";
inline constexpr const char* kMsgIndexKey   = "messageIndex";

}  // namespace mesh::config
