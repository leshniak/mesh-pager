#include "hal/TouchInput.h"
#include "config/UIConfig.h"

#include <M5Unified.h>
#include <algorithm>
#include <cstdlib>

namespace mesh::hal {

void TouchInput::init() {
    M5.Touch.setFlickThresh(config::ui::kSwipeThresholdPx);
}

TouchEvent TouchInput::processHoldOrSwipe(const m5::touch_detail_t& t, uint32_t now) {
    TouchEvent event;
    const int16_t dx = t.x - startX_;
    const int16_t dy = t.y - startY_;
    const auto absDx = std::abs(static_cast<int32_t>(dx));
    const auto absDy = std::abs(static_cast<int32_t>(dy));
    const auto threshold = static_cast<int32_t>(config::ui::kSwipeThresholdPx);

    if (absDx > threshold || absDy > threshold) {
        if (absDy > absDx) {
            event.gesture = (dy > 0) ? TouchGesture::SwipeDown : TouchGesture::SwipeUp;
        } else {
            event.gesture = (dx > 0) ? TouchGesture::SwipeRight : TouchGesture::SwipeLeft;
        }
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
    event.holdProgress = std::min(progress, 1.0f);
    return event;
}

TouchEvent TouchInput::update() {
    TouchEvent event;

    const auto count = M5.Touch.getCount();
    event.touching = (count > 0);

    if (count == 0) {
        // Finger lifted
        state_ = GestureState::Idle;
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

            startX_ = t.x;
            startY_ = t.y;
            startMs_ = now;
            state_ = GestureState::Tracking;
            break;
        }

        case GestureState::Tracking: {
            event = processHoldOrSwipe(t, now);
            if (event.gesture == TouchGesture::HoldTick) {
                state_ = GestureState::Holding;
            }
            return event;
        }

        case GestureState::Holding: {
            return processHoldOrSwipe(t, now);
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
