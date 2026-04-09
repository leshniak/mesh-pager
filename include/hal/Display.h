#pragma once

#include <string_view>

namespace mesh::hal {

/// Thin display abstraction. Current implementation uses M5.Display + M5.Log.
/// Future touch UI replaces this file only.
namespace display {

void init();
void sleep();
void wakeup();

/// Log an informational message to display + serial.
void logInfo(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

/// Log an error message to display + serial.
void logError(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

}  // namespace display
}  // namespace mesh::hal
