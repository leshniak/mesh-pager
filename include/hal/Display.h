#pragma once

#include <cstdarg>

namespace mesh::hal {
namespace display {

/// Initialize display hardware (brightness, orientation).
void init();

/// Turn off display backlight, enter low-power mode.
void sleep();

/// Wake display from sleep.
void wakeup();

/// Serial-only informational log (no longer draws to display).
void logInfo(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

/// Serial-only error log (no longer draws to display).
void logError(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

}  // namespace display
}  // namespace mesh::hal
