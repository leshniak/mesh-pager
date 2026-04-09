#include "app/AppState.h"
#include "config/AppConfig.h"

namespace mesh::app {

State nextState(State current, const InputEvents& events) {
    if (current != State::Idle) return current;

    // Priority: long press > timeout > double-click > single-click > rx
    if (events.longPress) {
        return State::PoweringOff;
    }

    if (events.timeSinceLastActionMs >= config::kSleepTimeoutMs && !events.isCharging) {
        return State::EnteringSleep;
    }

    if (events.doubleClick) {
        return State::Idle;  // message advance handled by caller
    }

    if (events.singleClick && events.timeSinceLastActionMs > config::kDebounceGuardMs) {
        return State::Transmitting;
    }

    if (events.rxReady) {
        return State::Receiving;
    }

    return State::Idle;
}

}  // namespace mesh::app
