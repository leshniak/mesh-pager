#include "app/AppState.h"
#include "config/AppConfig.h"

namespace mesh::app {

State nextState(State current, const InputEvents& events) {
    if (current != State::Idle) return current;

    // Priority: power off > sleep timeout > transmit > rx
    if (events.longPress) {
        return State::PoweringOff;
    }

    if (events.timeSinceLastActionMs >= config::kSleepTimeoutMs && !events.isCharging) {
        return State::EnteringSleep;
    }

    if (events.doubleClick) {
        return State::Idle;  // message advance handled by caller
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
