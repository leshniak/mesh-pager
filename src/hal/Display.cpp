#include "hal/Display.h"
#include "config/AppConfig.h"

#include <M5Unified.h>
#include <cstdarg>
#include <cstdio>

namespace mesh::hal::display {

/// Set portrait orientation (rotation 0 = USB port at bottom), full brightness,
/// and clear to black. Called once during setup().
void init() {
    M5.Display.setRotation(0);
    M5.Display.setBrightness(config::kBrightnessActive);
    M5.Display.fillScreen(TFT_BLACK);
}

/// Enter display low-power mode: turn off backlight, then wait for any pending
/// DMA transfers to complete before returning. This ensures the display isn't
/// left in a half-drawn state before deep sleep entry.
void sleep() {
    M5.Display.sleep();
    M5.Display.waitDisplay();
}

/// Wake the display controller and restore full brightness. Called when the
/// device wakes from sleep due to button press or charge-state change.
void wakeup() {
    M5.Display.wakeup();
    M5.Display.setBrightness(config::kBrightnessActive);
}

/// Dim the backlight for power saving during the inactivity window between
/// kDimTimeoutMs and kSleepTimeoutMs.
void dim() {
    M5.Display.setBrightness(config::kBrightnessDim);
}

/// Restore full brightness when the user touches the screen or presses a button.
void brighten() {
    M5.Display.setBrightness(config::kBrightnessActive);
}

/// Format and print an info-level message to Serial.
/// Uses a 256-byte stack buffer — messages longer than that are truncated.
void logInfo(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.printf("[INFO] %s\n", buf);
}

/// Format and print an error-level message to Serial.
void logError(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.printf("[ERROR] %s\n", buf);
}

}  // namespace mesh::hal::display
