#pragma once

/// Power management for the Arduino Nesso N1.
///
/// Handles three power-related concerns:
///   1. PMIC (AXP2101) charging configuration — current/voltage limits
///   2. Deep sleep entry — powers down all peripherals (display, radio, IMU,
///      touch controller) and configures EXT1 wakeup on KEY1 (via SYS_IRQ line)
///   3. Full power-off — tells the PMIC to cut power entirely; only USB
///      reconnect can bring the device back
///
/// Hardware topology for sleep:
///   - IO Expander 0 (0x43): Manages KEY1 button interrupt for wake
///   - IO Expander 1 (0x44): Controls LCD reset, backlight, LED, Grove power
///   - FT6336 touch controller (0x38): Put into hibernation mode
///   - BMI270 IMU (0x68/0x69): Suspended via power control registers
///   - LoRa SPI pins: Set to Hi-Z (INPUT) to avoid parasitic current
///   - ESP32 EXT1 wake: Triggered on SYS_IRQ going LOW (button press)

namespace mesh::hal {

namespace power {

/// Initialize I2C bus, configure IO expanders for KEY1 wake interrupt,
/// and set PMIC charging parameters (current limit + voltage).
void init();

/// Returns true if USB power is connected and the battery is charging.
/// Queries the AXP2101 PMIC via M5.Power.
bool isCharging();

/// Turn off the onboard LED (power indicator). Called before deep sleep
/// to minimize current draw.
void ledOff();

/// Full deep-sleep sequence:
///   1. Suspend IMU, put touch controller in hibernation
///   2. Power down display backlight, LCD, Grove, LED via IO expander
///   3. Set all SPI and peripheral GPIOs to Hi-Z
///   4. Disconnect I2C bus
///   5. Configure EXT1 wakeup on SYS_IRQ (KEY1 press)
///   6. Enter esp_deep_sleep_start()
/// Never returns. After wake, the device reboots from setup().
[[noreturn]] void enterDeepSleep();

/// Full power-off via PMIC. The AXP2101 cuts all power rails — the device
/// is completely off. Only reconnecting USB can restart it.
/// Never returns.
[[noreturn]] void powerOff();

}  // namespace power
}  // namespace mesh::hal
