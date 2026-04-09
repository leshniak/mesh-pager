#pragma once

#include <cstdarg>

namespace mesh::hal {
namespace display {

/// Initialize display hardware (brightness, orientation).
void init();

/// Turn off display backlight, enter low-power mode.
void sleep();

/// Wake display from sleep at full brightness.
void wakeup();

/// Reduce backlight brightness for power saving.
void dim();

/// Restore full brightness from dimmed state.
void brighten();

/// Serial-only informational log (no longer draws to display).
void logInfo(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

/// Serial-only error log (no longer draws to display).
void logError(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

}  // namespace display
}  // namespace mesh::hal
