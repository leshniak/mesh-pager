#pragma once

/// Audio feedback via the onboard piezo buzzer.
///
/// Each function plays a distinctive tone pattern so the user can distinguish
/// events without looking at the screen:
///   - TX: single mid-pitch beep (4000Hz)
///   - RX: single high-pitch beep (6000Hz)
///   - Sleep: two identical beeps (2500Hz)
///   - Power-off: three descending beeps (6000→4000→2500Hz)
///
/// All functions are blocking — they use delay() between notes and return
/// only after the pattern finishes. This is acceptable because they are called
/// at state transitions where a brief pause is expected.

namespace mesh::hal {

/// Play a single short beep to confirm message transmission.
void playTxTone();

/// Play a single short high-pitch beep for incoming message notification.
void playRxTone();

/// Play two beeps to signal deep sleep entry.
void playSleepTone();

/// Play three descending beeps to signal full power-off via PMIC.
void playPowerOffTone();

}  // namespace mesh::hal
