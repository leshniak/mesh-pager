#include "hal/TouchInput.h"
#include "config/UIConfig.h"

#include <M5Unified.h>

namespace mesh::hal {

void TouchInput::init() {
    M5.Touch.setFlickThresh(config::ui::kSwipeThresholdPx);
}

TouchEvent TouchInput::update() {
    TouchEvent event;

    const auto count = M5.Touch.getCount();
    event.touching = (count > 0);

    if (count == 0) {
        // Finger lifted
        if (state_ == GestureState::Swiping) {
            state_ = GestureState::Idle;
        }
        if (state_ != GestureState::Idle) {
            state_ = GestureState::Idle;
        }
        return event;
    }

    const auto t = M5.Touch.getDetail();
    const uint32_t now = millis();

    switch (state_) {
        case GestureState::Idle: {
            if (!t.wasPressed()) break;

            if (consumeNextTouch_) {
                consumeNextTouch_ = false;
                event.gesture = TouchGesture::Wake;
                state_ = GestureState::Cooldown;
                return event;
            }

            if (t.y < config::ui::kStatusBarHeight) {
                event.gesture = TouchGesture::StatusBarTap;
                state_ = GestureState::Cooldown;
                return event;
            }

            startX_ = t.x;
            startY_ = t.y;
            startMs_ = now;
            state_ = GestureState::Tracking;
            break;
        }

        case GestureState::Tracking: {
            const int16_t dx = t.x - startX_;

            if (abs(dx) > static_cast<int16_t>(config::ui::kSwipeThresholdPx)) {
                event.gesture = (dx > 0) ? TouchGesture::SwipeRight : TouchGesture::SwipeLeft;
                state_ = GestureState::Swiping;
                return event;
            }

            const uint32_t elapsed = now - startMs_;
            const float progress = static_cast<float>(elapsed) / config::ui::kHoldToSendMs;

            if (elapsed >= config::ui::kHoldToSendMs) {
                event.gesture = TouchGesture::HoldComplete;
                event.holdProgress = 1.0f;
                state_ = GestureState::Cooldown;
                return event;
            }

            event.gesture = TouchGesture::HoldTick;
            event.holdProgress = progress;
            state_ = GestureState::Holding;
            return event;
        }

        case GestureState::Holding: {
            const int16_t dx = t.x - startX_;

            if (abs(dx) > static_cast<int16_t>(config::ui::kSwipeThresholdPx)) {
                event.gesture = (dx > 0) ? TouchGesture::SwipeRight : TouchGesture::SwipeLeft;
                state_ = GestureState::Swiping;
                return event;
            }

            const uint32_t elapsed = now - startMs_;
            const float progress = static_cast<float>(elapsed) / config::ui::kHoldToSendMs;

            if (elapsed >= config::ui::kHoldToSendMs) {
                event.gesture = TouchGesture::HoldComplete;
                event.holdProgress = 1.0f;
                state_ = GestureState::Cooldown;
                return event;
            }

            event.gesture = TouchGesture::HoldTick;
            event.holdProgress = progress;
            return event;
        }

        case GestureState::Swiping:
            break;

        case GestureState::Cooldown:
            break;
    }

    return event;
}

void TouchInput::consumeNextTouch() {
    consumeNextTouch_ = true;
}

}  // namespace mesh::hal
