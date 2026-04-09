#include "hal/Display.h"

#include <M5Unified.h>
#include <cstdarg>

namespace mesh::hal::display {

void init() {
    M5.setLogDisplayIndex(0);
    M5.Display.setTextWrap(true, true);
    M5.Display.setTextScroll(true);

    M5.Log.setLogLevel(m5::log_target_serial, ESP_LOG_VERBOSE);
    M5.Log.setLogLevel(m5::log_target_display, ESP_LOG_DEBUG);

    M5.Log.setEnableColor(m5::log_target_serial, false);
    M5.Log.setEnableColor(m5::log_target_display, true);

    M5.Log.setSuffix(m5::log_target_serial, "\n");
    M5.Log.setSuffix(m5::log_target_display, "\n");
}

void sleep() {
    M5.Display.sleep();
    M5.Display.waitDisplay();
}

void wakeup() {
    M5.Display.wakeup();
}

void logInfo(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    M5.Log(ESP_LOG_INFO, "%s", buf);
}

void logError(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    M5.Log(ESP_LOG_ERROR, "%s", buf);
}

}  // namespace mesh::hal::display
