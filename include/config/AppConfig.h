#pragma once

/// Application-wide configuration constants.
///
/// All tunables live here so they can be adjusted in one place without touching
/// implementation files. Values are `inline constexpr` to guarantee zero-cost
/// at runtime (folded into immediates by the compiler).

#include <cstdint>

namespace mesh::config {

// ── Power management ────────────────────────────────────────────────────────
// These control the AXP2101 PMIC charging parameters and the display/CPU
// power-saving thresholds. The Nesso N1 battery is a small 180mAh LiPo,
// so conservative charging and aggressive sleep are important.

inline constexpr uint16_t kChargeCurrent     = 256;   ///< PMIC charge current limit (mA)
inline constexpr uint16_t kChargeVoltage     = 4200;  ///< PMIC charge termination voltage (mV, 4.2V = standard LiPo)
inline constexpr uint32_t kSleepTimeoutMs    = 15000;  ///< Enter deep sleep after this many ms of inactivity
inline constexpr uint32_t kStayAwakeMaxMs   = 600000; ///< Stay-awake lock auto-disables after 10 minutes
inline constexpr uint32_t kDebounceGuardMs   = 1000;  ///< Ignore spurious inputs for this long after wake
inline constexpr uint16_t kButtonHoldMs      = 1000;  ///< Physical button hold duration to trigger power-off
inline constexpr uint8_t  kCpuFreqMHz        = 80;    ///< CPU clock (reduced from 160MHz default; saves ~25% power)
inline constexpr uint8_t  kLoopIdleDelayMs   = 10;    ///< delay() in idle loop — lets CPU enter automatic light sleep

// ── Buzzer tones ────────────────────────────────────────────────────────────
// Single-frequency square-wave tones played through the onboard piezo buzzer
// via M5.Speaker.tone(). Higher pitch = more attention-getting.

inline constexpr uint16_t kTxToneHz          = 4000;  ///< Transmit confirmation beep (single pulse)
inline constexpr uint16_t kRxToneHz          = 6000;  ///< Receive notification beep (single pulse)
inline constexpr uint16_t kToneDurationMs    = 50;    ///< Duration for TX/RX single-pulse tones

/// Sleep tone: two identical beeps with a gap (descending feel due to repetition).
inline constexpr uint16_t kSleepToneHz       = 2500;
inline constexpr uint16_t kSleepToneDurMs    = 100;
inline constexpr uint16_t kSleepToneGapMs    = 150;

/// Power-off tone: three descending beeps (6000 → 4000 → 2500 Hz).
/// The descending pitch signals "shutting down" to the user.
inline constexpr uint16_t kPowerOffTone1Hz   = 6000;
inline constexpr uint16_t kPowerOffTone2Hz   = 4000;
inline constexpr uint16_t kPowerOffTone3Hz   = 2500;
inline constexpr uint16_t kPowerOffToneDurMs = 100;
inline constexpr uint16_t kPowerOffToneGapMs = 150;

// ── Meshtastic mesh flags ───────────────────────────────────────────────────
// These are packed into the 8-bit flags byte of the mesh header (see MeshTypes.h).
// hopLimit/hopStart control how many relay hops a packet can take before being
// dropped. Setting both to 3 is the Meshtastic default for LongFast.

inline constexpr uint8_t kHopLimit  = 3;     ///< Remaining hops (decremented by each relay)
inline constexpr uint8_t kHopStart  = 3;     ///< Original hop count (stored for duplicate detection)
inline constexpr bool kWantAck      = false;  ///< Request ACK from destination (broadcast doesn't ACK)
inline constexpr bool kViaMqtt      = false;  ///< Packet originated from MQTT gateway (always false for us)

// ── Send-on-wake feature ────────────────────────────────────────────────────
// When enabled, the device transmits the current canned message immediately
// after waking from deep sleep (but NOT on cold boot). Useful as a gate/garage
// remote where pressing the button = wake + send + sleep cycle.
// Guarded with `if constexpr` so disabled code is completely eliminated.

inline constexpr bool kSendOnWake   = false;  ///< Auto-transmit on deep-sleep wake (default: off)

// ── Serial ──────────────────────────────────────────────────────────────────
// Note: USB-JTAG CDC on ESP32-C6 is unreliable after deep sleep — the port
// disappears. Serial output is best-effort; on-screen toasts are the primary
// debug channel on this hardware.

inline constexpr uint32_t kSerialBaudRate = 115200;  ///< UART baud rate for Serial.begin()

// ── NVS preferences ─────────────────────────────────────────────────────────
// The ESP32 NVS (Non-Volatile Storage) partition stores the currently selected
// canned message index so it survives deep sleep and power cycles.

inline constexpr const char* kAppName       = "mesh-remote";    ///< NVS namespace (max 15 chars)
inline constexpr const char* kMsgIndexKey   = "messageIndex";   ///< NVS key for persisted message index

}  // namespace mesh::config
