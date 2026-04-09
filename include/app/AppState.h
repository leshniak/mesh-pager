#pragma once

#include <cstdint>

namespace mesh::app {

enum class State : uint8_t {
    Idle,
    Transmitting,
    Receiving,
    EnteringSleep,
    PoweringOff,
};

struct InputEvents {
    bool holdComplete    = false;  // touch hold-to-send reached 1000ms
    bool longPress       = false;  // KEY1 physical button hold (power off)
    bool touchActive     = false;
    bool chargingChanged = false;
    bool rxReady         = false;
    bool isCharging      = false;
    uint32_t timeSinceLastActionMs = 0;
};

/// Determine next state given current state and input events.
/// Returns the new state. Caller is responsible for executing side effects.
State nextState(State current, const InputEvents& events);

}  // namespace mesh::app
