#pragma once

#include <cstdint>

namespace mesh::hal {

enum class TouchGesture : uint8_t {
    None,
    SwipeLeft,
    SwipeRight,
    HoldTick,
    HoldComplete,
    StatusBarTap,
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
    TouchEvent update();
    void consumeNextTouch();

private:
    enum class GestureState : uint8_t {
        Idle,
        Tracking,
        Swiping,
        Holding,
        Cooldown,
    };

    GestureState state_ = GestureState::Idle;
    int16_t startX_ = 0;
    int16_t startY_ = 0;
    uint32_t startMs_ = 0;
    bool consumeNextTouch_ = false;
};

}  // namespace mesh::hal
