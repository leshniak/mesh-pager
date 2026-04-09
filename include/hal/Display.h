#pragma once

/// Display hardware abstraction layer.
///
/// Wraps M5Unified's display API to provide brightness management and
/// sleep/wake control for the ST7789 135×240 TFT. The display is driven
/// via M5GFX (LGFX); this module only manages the backlight and power state.
///
/// Rendering is handled separately by ui::Renderer, which draws into an
/// LGFX_Sprite (double-buffer) and pushes the completed frame to the display.
///
/// Logging functions output to USB-CDC Serial only. On ESP32-C6 with USB-JTAG,
/// serial is unreliable after deep sleep (the port disappears), so these are
/// best-effort debug aids — not the primary user feedback channel.

#include <cstdarg>

namespace mesh::hal {
namespace display {

/// Set portrait orientation, full brightness, and clear the screen to black.
void init();

/// Turn off the display backlight and put the controller into low-power mode.
/// After this call the screen is dark. Call wakeup() to restore.
void sleep();

/// Re-enable the display controller and restore full brightness.
void wakeup();

/// Reduce backlight to kBrightnessDim — used during the inactivity dim period.
void dim();

/// Restore backlight to kBrightnessActive — called when user interaction resumes.
void brighten();

/// Log a formatted info message to Serial. Printf-style format string.
/// No-op visible to the user; used for USB-CDC debug output only.
void logInfo(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

/// Log a formatted error message to Serial. Printf-style format string.
void logError(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

}  // namespace display
}  // namespace mesh::hal
