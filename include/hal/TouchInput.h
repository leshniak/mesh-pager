#pragma once

#include <cstdint>

#include <M5Unified.h>

namespace mesh::hal {

enum class TouchGesture : uint8_t {
    None,
    SwipeLeft,
    SwipeRight,
    SwipeDown,
    SwipeUp,
    HoldTick,
    HoldComplete,
    Wake,
};

struct TouchEvent {
    TouchGesture gesture = TouchGesture::None;
    float holdProgress = 0.0f;
    bool touching = false;
};

class TouchInput {
public:
    void init();

    /// Call once per loop iteration after M5.update().
    TouchEvent update();

    /// Next touch-begin will be consumed as a wake event (no gesture).
    void consumeNextTouch();

private:
    enum class GestureState : uint8_t {
        Idle,
        Tracking,
        Swiping,
        Holding,
        Cooldown,
    };

    /// Shared logic for Tracking and Holding states: detect swipe or emit hold progress.
    TouchEvent processHoldOrSwipe(const m5::touch_detail_t& t, uint32_t now);

    GestureState state_ = GestureState::Idle;
    int16_t startX_ = 0;
    int16_t startY_ = 0;
    uint32_t startMs_ = 0;
    bool consumeNextTouch_ = false;
};

}  // namespace mesh::hal
