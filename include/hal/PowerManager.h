#pragma once

namespace mesh::hal {

namespace power {

/// Configure charging parameters and initialize deep sleep hardware.
void init();

/// Returns true if USB power is connected and charging.
bool isCharging();

/// Turn off LED indicator.
void ledOff();

/// Power down all peripherals and enter ESP32 deep sleep.
/// Wakes on KEY1 press via SYS_IRQ.
[[noreturn]] void enterDeepSleep();

/// Full power-off via PMIC.
[[noreturn]] void powerOff();

}  // namespace power
}  // namespace mesh::hal
