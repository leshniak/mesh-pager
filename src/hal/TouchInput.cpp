#include "hal/TouchInput.h"
#include "config/UIConfig.h"

#include <M5Unified.h>
#include <algorithm>
#include <cstdlib>

namespace mesh::hal {

/// Configure M5Unified's internal flick detection threshold. This value is also
/// used by our own swipe detection in processHoldOrSwipe().
void TouchInput::init() {
    M5.Touch.setFlickThresh(config::ui::kSwipeThresholdPx);
}

/// Core gesture disambiguation: called each frame while the finger is down.
///
/// Decision logic:
///   1. If finger displacement exceeds kSwipeThresholdPx in any direction,
///      classify as a swipe (dominant axis wins) and transition to Swiping.
///   2. Otherwise, compute hold progress as elapsed / kHoldToSendMs.
///      - If ≥ 1.0 → HoldComplete, transition to Cooldown.
///      - If < 1.0 → HoldTick (caller draws progress bar).
TouchEvent TouchInput::processHoldOrSwipe(const m5::touch_detail_t& t, uint32_t now) {
    TouchEvent event;
    const int16_t dx = t.x - startX_;
    const int16_t dy = t.y - startY_;
    const auto absDx = std::abs(static_cast<int32_t>(dx));
    const auto absDy = std::abs(static_cast<int32_t>(dy));
    const auto threshold = static_cast<int32_t>(config::ui::kSwipeThresholdPx);

    // Check for swipe (displacement exceeds threshold)
    if (absDx > threshold || absDy > threshold) {
        // Classify by dominant axis
        if (absDy > absDx) {
            event.gesture = (dy > 0) ? TouchGesture::SwipeDown : TouchGesture::SwipeUp;
        } else {
            event.gesture = (dx > 0) ? TouchGesture::SwipeRight : TouchGesture::SwipeLeft;
        }
        state_ = GestureState::Swiping;  // Swipe detected — stop tracking
        return event;
    }

    // No swipe — compute hold progress
    const uint32_t elapsed = now - startMs_;
    const float progress = static_cast<float>(elapsed) / config::ui::kHoldToSendMs;

    if (elapsed >= config::ui::kHoldToSendMs) {
        event.gesture = TouchGesture::HoldComplete;
        event.holdProgress = 1.0f;
        state_ = GestureState::Cooldown;  // Hold complete — ignore until lift
        return event;
    }

    event.gesture = TouchGesture::HoldTick;
    event.holdProgress = std::min(progress, 1.0f);
    return event;
}

/// Main update function — call once per loop iteration after M5.update().
///
/// State machine flow:
///   - **No touch**: Reset to Idle immediately (finger lifted).
///   - **Idle + touch begin**: Record start position/time, enter Tracking.
///     If consumeNextTouch_ is set, emit Wake and skip to Cooldown.
///   - **Tracking**: First frame after touch-down — call processHoldOrSwipe().
///     If it's a HoldTick, promote to Holding (subsequent frames skip the
///     initial "is this a swipe?" ambiguity since we already decided).
///   - **Holding**: Continue calling processHoldOrSwipe() until HoldComplete
///     or finger lift.
///   - **Swiping/Cooldown**: Ignore all input until finger lifts (returns to Idle).
TouchEvent TouchInput::update() {
    TouchEvent event;

    const auto count = M5.Touch.getCount();
    event.touching = (count > 0);

    if (count == 0) {
        state_ = GestureState::Idle;  // Finger lifted — reset state machine
        return event;
    }

    const auto t = M5.Touch.getDetail();
    const uint32_t now = millis();

    switch (state_) {
        case GestureState::Idle: {
            if (!t.wasPressed()) break;  // Spurious report — no actual press

            // If flagged, consume this touch as a wake event (no action)
            if (consumeNextTouch_) {
                consumeNextTouch_ = false;
                event.gesture = TouchGesture::Wake;
                state_ = GestureState::Cooldown;
                return event;
            }

            // Record touch-down origin for swipe/hold detection
            startX_ = t.x;
            startY_ = t.y;
            startMs_ = now;
            state_ = GestureState::Tracking;
            break;
        }

        case GestureState::Tracking: {
            // First update after touch-down — disambiguate swipe vs hold
            event = processHoldOrSwipe(t, now);
            if (event.gesture == TouchGesture::HoldTick) {
                state_ = GestureState::Holding;  // Committed to hold gesture
            }
            return event;
        }

        case GestureState::Holding: {
            // Continue hold progress updates until complete or finger lift
            return processHoldOrSwipe(t, now);
        }

        case GestureState::Swiping:
            break;  // Ignore movement after swipe detected

        case GestureState::Cooldown:
            break;  // Ignore everything after hold completed
    }

    return event;
}

/// Flag the next touch-begin as a Wake event. Called before display sleep
/// so that the touch that wakes the screen doesn't accidentally trigger
/// a swipe or message send.
void TouchInput::consumeNextTouch() {
    consumeNextTouch_ = true;
}

}  // namespace mesh::hal
