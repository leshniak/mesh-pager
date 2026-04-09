#include "hal/Display.h"

#include <M5Unified.h>
#include <cstdarg>
#include <cstdio>

namespace mesh::hal::display {

void init() {
    M5.Display.setRotation(0);  // Portrait
    M5.Display.setBrightness(80);
    M5.Display.fillScreen(TFT_BLACK);
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
    Serial.printf("[INFO] %s\n", buf);
}

void logError(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.printf("[ERROR] %s\n", buf);
}

}  // namespace mesh::hal::display
