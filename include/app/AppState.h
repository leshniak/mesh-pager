#pragma once

/// Application state machine.
///
/// The state machine is pure — it takes the current state and input events,
/// and returns the next state. All side effects (radio TX, display updates,
/// sleep entry) are handled by the caller in main.cpp based on the returned state.
///
/// State transitions (from Idle only):
///   longPress                          → PoweringOff
///   inactivity >= kSleepTimeoutMs      → EnteringSleep (unless charging)
///   holdComplete or singleClick        → Transmitting
///   rxReady                            → Receiving
///   doubleClick                        → Idle (stay-awake toggle handled by caller)

#include <cstdint>

namespace mesh::app {

enum class State : uint8_t {
    Idle,            ///< Normal operation — waiting for user input or radio RX
    Transmitting,    ///< Sending a canned message (handled as single blocking TX)
    Receiving,       ///< Processing an incoming radio packet
    EnteringSleep,   ///< Transitioning to deep sleep (display off, radio off)
    PoweringOff,     ///< Full power-off via PMIC (no wake without USB)
};

/// Input events collected from touch, physical button, radio, and timers.
/// All fields are sampled once per loop iteration in main.cpp.
struct InputEvents {
    bool singleClick     = false;  ///< Physical button single click → transmit
    bool doubleClick     = false;  ///< Physical button double click → toggle stay-awake
    bool holdComplete    = false;  ///< Touch hold reached 1000ms → transmit
    bool longPress       = false;  ///< Physical button held → power off
    bool touchActive     = false;  ///< Finger currently on screen
    bool chargingChanged = false;  ///< USB charge state just changed
    bool rxReady         = false;  ///< Radio has a pending received packet
    bool isCharging      = false;  ///< USB power connected
    uint32_t timeSinceLastActionMs = 0;  ///< Milliseconds since last user interaction
};

/// Determine next state given current state and input events.
/// Only transitions from Idle — all other states return unchanged (handled by caller).
State nextState(State current, const InputEvents& events);

}  // namespace mesh::app
