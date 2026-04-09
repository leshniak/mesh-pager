#include "app/AppState.h"
#include "config/AppConfig.h"

namespace mesh::app {

State nextState(State current, const InputEvents& events) {
    // Only transition from Idle. Other states are transient — the caller
    // executes the action and immediately returns to Idle.
    if (current != State::Idle) return current;

    // Priority order: power off > sleep > transmit > receive
    // This ensures safety-critical actions (power off) always take precedence.

    if (events.longPress) {
        return State::PoweringOff;
    }

    if (events.timeSinceLastActionMs >= config::kSleepTimeoutMs && !events.isCharging) {
        return State::EnteringSleep;
    }

    if (events.doubleClick) {
        return State::Idle;  // message advance is handled directly by caller
    }

    if (events.holdComplete) {
        return State::Transmitting;
    }

    if (events.singleClick) {
        return State::Transmitting;
    }

    if (events.rxReady) {
        return State::Receiving;
    }

    return State::Idle;
}

}  // namespace mesh::app
