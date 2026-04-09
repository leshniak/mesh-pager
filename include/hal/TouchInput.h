#pragma once

/// Capacitive touch gesture recognizer.
///
/// Processes raw touch events from the FT6336 touch controller (via M5Unified)
/// and classifies them into discrete gestures:
///
///   - **Swipe left/right**: Navigate between canned messages
///   - **Swipe down**: Open message history overlay
///   - **Swipe up**: Close message history overlay
///   - **Hold (1s)**: Transmit the current canned message (with progress feedback)
///   - **Wake**: First touch after deep sleep (consumed, no action)
///
/// Gesture recognition state machine:
///
///   Idle ──[touch begin]──→ Tracking
///     │                        │
///     │                   [displacement > threshold] → Swiping (emit SwipeX, then ignore)
///     │                        │
///     │                   [elapsed > 0] → Holding (emit HoldTick with progress)
///     │                        │
///     │                   [elapsed ≥ kHoldToSendMs] → Cooldown (emit HoldComplete)
///     │                        │
///     ←──[finger lifted]───────┘ (all states return to Idle on lift)
///
/// The `consumeNextTouch_` flag is set before deep sleep / display sleep so
/// that the wake-up touch doesn't accidentally trigger a swipe or transmit.

#include <cstdint>

#include <M5Unified.h>

namespace mesh::hal {

/// Discrete gesture types emitted by TouchInput::update().
enum class TouchGesture : uint8_t {
    None,           ///< No gesture detected this frame
    SwipeLeft,      ///< Finger moved left past threshold → previous message
    SwipeRight,     ///< Finger moved right past threshold → next message
    SwipeDown,      ///< Finger moved down past threshold → open history
    SwipeUp,        ///< Finger moved up past threshold → close history
    HoldTick,       ///< Finger held in place — holdProgress updated (0.0–1.0)
    HoldComplete,   ///< Hold duration reached kHoldToSendMs → transmit
    Wake,           ///< Post-sleep touch consumed (no user-visible action)
};

/// Result of a single update() call. Contains the detected gesture, hold
/// progress (only meaningful for HoldTick), and whether any finger is touching.
struct TouchEvent {
    TouchGesture gesture = TouchGesture::None;
    float holdProgress = 0.0f;  ///< 0.0–1.0, valid during HoldTick/HoldComplete
    bool touching = false;       ///< True if any finger is on the screen
};

class TouchInput {
public:
    /// Configure M5Unified's flick threshold for swipe detection.
    void init();

    /// Sample the touch controller and return any detected gesture.
    /// Must be called once per loop iteration, after M5.update().
    TouchEvent update();

    /// Mark the next touch-begin as a Wake gesture (consumed without action).
    /// Called before display sleep so the wake-up touch doesn't trigger a send.
    void consumeNextTouch();

private:
    /// Internal gesture recognizer states.
    enum class GestureState : uint8_t {
        Idle,       ///< No finger touching — waiting for touch-begin
        Tracking,   ///< Finger down, deciding between swipe and hold
        Swiping,    ///< Swipe detected — ignore further movement until lift
        Holding,    ///< Hold in progress — emitting HoldTick each frame
        Cooldown,   ///< HoldComplete fired — ignore until finger lifts
    };

    /// Shared logic for Tracking → Swiping/Holding transitions.
    /// Checks displacement for swipe, or elapsed time for hold progress.
    TouchEvent processHoldOrSwipe(const m5::touch_detail_t& t, uint32_t now);

    GestureState state_ = GestureState::Idle;
    int16_t startX_ = 0;       ///< Touch-down X coordinate (px)
    int16_t startY_ = 0;       ///< Touch-down Y coordinate (px)
    uint32_t startMs_ = 0;     ///< Touch-down timestamp (millis)
    bool consumeNextTouch_ = false;  ///< If true, next touch-begin → Wake
};

}  // namespace mesh::hal
