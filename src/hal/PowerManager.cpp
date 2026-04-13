#include "hal/PowerManager.h"
#include "config/Pins.h"
#include "config/AppConfig.h"

#include <M5Unified.h>
#include <Wire.h>
#include <esp_sleep.h>

namespace mesh::hal::power {

// ============================================================================
// I2C register maps for Nesso N1 peripherals
// ============================================================================
//
// The Nesso N1 uses two PI4IOE5V6408 I/O expanders on I2C to manage
// peripherals that don't have dedicated GPIOs:
//   - Expander0 (0x43): Input-only — reads KEY1 button and provides interrupt
//   - Expander1 (0x44): Output-only — controls LCD reset, backlight, LED, Grove
//
// The touch controller (FT6336) and IMU (BMI270) also live on the same I2C bus
// and need explicit power-down commands before deep sleep.

namespace {

/// IO Expander 0 — handles KEY1 button detection and wake interrupt.
/// This expander's interrupt output is wired to SYS_IRQ, which is used as
/// the EXT1 wakeup source for deep sleep.
struct Expander0 {
    static constexpr uint8_t addr = 0x43;          ///< I2C address
    struct Reg {
        static constexpr uint8_t ioDir        = 0x03; ///< I/O direction (0=output, 1=input)
        static constexpr uint8_t inputDefault = 0x09; ///< Default input comparison value for IRQ
        static constexpr uint8_t pullEnable   = 0x0B; ///< Internal pull-up/down enable
        static constexpr uint8_t pullSelect   = 0x0D; ///< Pull direction (1=pull-up, 0=pull-down)
        static constexpr uint8_t inputStatus  = 0x0F; ///< Current pin levels
        static constexpr uint8_t intMask      = 0x11; ///< Interrupt mask (0=enabled, 1=masked)
        static constexpr uint8_t intStatus    = 0x13; ///< Interrupt status (read to clear latch)
    };
    static constexpr uint8_t key1Bit = 0;           ///< KEY1 is on bit 0 (active LOW)
};

/// IO Expander 1 — controls display, LED, and Grove power rails.
struct Expander1 {
    static constexpr uint8_t addr = 0x44;
    struct Reg {
        static constexpr uint8_t outputPort = 0x01;   ///< Output port register
        static constexpr uint8_t ioDir      = 0x03;   ///< I/O direction
    };
    static constexpr uint8_t lcdResetBit     = 1;     ///< LCD controller reset (active LOW)
    static constexpr uint8_t grovePowerEnBit = 2;     ///< Grove connector 3.3V rail
    static constexpr uint8_t backlightBit    = 6;     ///< LCD backlight enable
    static constexpr uint8_t ledBit          = 7;     ///< Status LED
};

/// FT6336 capacitive touch controller — supports hibernation mode for sleep.
struct Touch {
    static constexpr uint8_t addr = 0x38;
    struct Reg {
        static constexpr uint8_t tdStatus = 0x02;     ///< Number of touch points (read clears IRQ)
        static constexpr uint8_t gMode    = 0xA4;     ///< Gesture mode (trigger vs continuous)
        static constexpr uint8_t pwrMode  = 0xA5;     ///< Power mode (0=active, 3=hibernate)
    };
    struct Val {
        static constexpr uint8_t gModeTrigger = 0x01;  ///< Trigger mode — report once per gesture
        static constexpr uint8_t pwrHibernate = 0x03;  ///< Hibernate — lowest power, needs reset to wake
    };
};

/// BMI270 IMU — can be at address 0x68 or 0x69 depending on SDO pin.
/// We probe both and suspend whichever responds.
struct Imu {
    static constexpr uint8_t addr0 = 0x68;
    static constexpr uint8_t addr1 = 0x69;
    struct Reg {
        static constexpr uint8_t chipId  = 0x00;      ///< Chip ID register (used to detect presence)
        static constexpr uint8_t pwrConf = 0x7C;      ///< Power configuration
        static constexpr uint8_t pwrCtrl = 0x7D;      ///< Power control (accel/gyro/aux enable)
    };
    struct Val {
        static constexpr uint8_t pwrCtrlAllOff      = 0x00;  ///< Disable all sensor blocks
        static constexpr uint8_t pwrConfAdvPowerSave = 0x01;  ///< Enter advanced power save mode
    };
};

// ============================================================================
// I2C register helpers
// ============================================================================
// Low-level single-byte I2C read/write. Used throughout for talking to the
// IO expanders, touch controller, and IMU. All return false on NACK.

/// Write a single byte to an I2C register.
bool writeReg8(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

/// Read a single byte from an I2C register. Uses a repeated-start (no STOP
/// between write and read) for atomicity.
bool readReg8(uint8_t addr, uint8_t reg, uint8_t& val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;  // repeated-start
    if (Wire.requestFrom(static_cast<int>(addr), 1) != 1) return false;
    val = static_cast<uint8_t>(Wire.read());
    return true;
}

/// Read the SYS_IRQ GPIO level. This pin is shared between the IO expander
/// interrupt output and the touch controller — it goes LOW when either has
/// a pending event.
int readSysIrqLevel() {
    return digitalRead(static_cast<int>(SYS_IRQ));
}

// ============================================================================
// IO Expander helpers
// ============================================================================

/// Clear the interrupt latch on Expander0 by reading both intStatus and
/// inputStatus. The PI4IOE5V6408 requires reading these registers to deassert
/// the IRQ output — otherwise SYS_IRQ stays LOW and prevents wakeup.
void clearExpanderIrqLatch() {
    uint8_t dummy = 0;
    (void)readReg8(Expander0::addr, Expander0::Reg::intStatus, dummy);
    (void)readReg8(Expander0::addr, Expander0::Reg::inputStatus, dummy);
}

/// Check if KEY1 is physically pressed right now by reading the input port.
/// KEY1 is active LOW — returns true when the button is held down.
bool isKey1PressedNow() {
    uint8_t inputs = 0;
    if (!readReg8(Expander0::addr, Expander0::Reg::inputStatus, inputs)) return false;
    return (inputs & (1u << Expander0::key1Bit)) == 0;
}

/// Configure Expander0 to generate an interrupt on KEY1 press.
/// Sets up:
///   - KEY1 pin as input
///   - Internal pull-up enabled (button pulls LOW when pressed)
///   - Default comparison value = HIGH (interrupt fires on LOW transition)
///   - Interrupt mask: only KEY1 unmasked (all other bits masked)
/// Finally clears any pending latch to start clean.
void configureExpanderKey1Interrupt() {
    uint8_t v = 0;

    // Set KEY1 bit as input (clear the bit in ioDir)
    if (readReg8(Expander0::addr, Expander0::Reg::ioDir, v)) {
        v &= ~(1u << Expander0::key1Bit);
        (void)writeReg8(Expander0::addr, Expander0::Reg::ioDir, v);
    } else {
        (void)writeReg8(Expander0::addr, Expander0::Reg::ioDir, 0x00);
    }

    // Enable internal pull-up on KEY1
    if (readReg8(Expander0::addr, Expander0::Reg::pullEnable, v)) {
        v |= (1u << Expander0::key1Bit);
        (void)writeReg8(Expander0::addr, Expander0::Reg::pullEnable, v);
    }

    // Select pull-up direction (1 = pull-up)
    if (readReg8(Expander0::addr, Expander0::Reg::pullSelect, v)) {
        v |= (1u << Expander0::key1Bit);
        (void)writeReg8(Expander0::addr, Expander0::Reg::pullSelect, v);
    }

    // Set default input level = HIGH (interrupt triggers on change from default)
    if (readReg8(Expander0::addr, Expander0::Reg::inputDefault, v)) {
        v |= (1u << Expander0::key1Bit);
        (void)writeReg8(Expander0::addr, Expander0::Reg::inputDefault, v);
    }

    // Unmask only KEY1's interrupt; mask all other bits (0xFF with KEY1 bit cleared)
    uint8_t mask = 0xFF;
    mask &= ~(1u << Expander0::key1Bit);
    (void)writeReg8(Expander0::addr, Expander0::Reg::intMask, mask);

    clearExpanderIrqLatch();
}

/// Set a single output bit on Expander1. Reads current direction and output
/// registers, modifies the target bit, and writes both back.
/// Used to control LCD reset, backlight, LED, and Grove power individually.
void expander1WriteOutputBit(uint8_t bit, bool high) {
    uint8_t dir = 0, out = 0;
    (void)readReg8(Expander1::addr, Expander1::Reg::ioDir, dir);
    (void)readReg8(Expander1::addr, Expander1::Reg::outputPort, out);

    dir |= (1u << bit);                    // Set bit as output
    if (high) out |= (1u << bit);          // Drive HIGH
    else      out &= ~(1u << bit);         // Drive LOW

    (void)writeReg8(Expander1::addr, Expander1::Reg::ioDir, dir);
    (void)writeReg8(Expander1::addr, Expander1::Reg::outputPort, out);
}

// ============================================================================
// Touch controller helpers (FT6336)
// ============================================================================

/// Clear any pending touch interrupt by reading the touch point count register.
/// Best-effort: ignores failures since we're about to hibernate the controller.
void clearTouchIrqBestEffort() {
    uint8_t v = 0;
    (void)readReg8(Touch::addr, Touch::Reg::tdStatus, v);
}

/// Put the FT6336 touch controller into hibernation (lowest power mode).
/// Sequence: switch to trigger mode → clear pending IRQ → enter hibernate.
/// In hibernate, the controller draws ~10µA but requires a reset pulse to wake.
void touchEnterHibernation() {
    (void)writeReg8(Touch::addr, Touch::Reg::gMode, Touch::Val::gModeTrigger);
    clearTouchIrqBestEffort();
    (void)writeReg8(Touch::addr, Touch::Reg::pwrMode, Touch::Val::pwrHibernate);
    delay(5);  // Give controller time to enter hibernate
}

/// Wake the FT6336 by pulsing SYS_IRQ LOW for ~1ms. The touch controller
/// uses this pin as a reset input — a LOW pulse brings it out of hibernation.
/// After the pulse, SYS_IRQ is returned to INPUT_PULLUP for normal operation
/// (shared with IO expander interrupt output).
///
/// The expander IRQ latch is cleared before and after the pulse to avoid
/// a stale LOW from the expander interfering with the touch wake sequence.
void touchWakePulseOnSysIrq() {
    clearExpanderIrqLatch();       // Clear any pending expander IRQ first
    pinMode(static_cast<int>(SYS_IRQ), OUTPUT);
    digitalWrite(static_cast<int>(SYS_IRQ), LOW);
    delay(1);
    pinMode(static_cast<int>(SYS_IRQ), INPUT_PULLUP);
    delay(5);  // Wait for touch controller to initialize
    clearExpanderIrqLatch();       // Clear any IRQ generated during the pulse
}

// ============================================================================
// IMU helpers (BMI270)
// ============================================================================

/// Suspend a BMI270 at the given I2C address by disabling all sensor blocks
/// and entering advanced power save mode. The 600µs delays are required by
/// the BMI270 datasheet between power register writes.
bool imuEnterSuspendI2c(uint8_t addr) {
    if (!writeReg8(addr, Imu::Reg::pwrCtrl, Imu::Val::pwrCtrlAllOff)) return false;
    delayMicroseconds(600);
    if (!writeReg8(addr, Imu::Reg::pwrConf, Imu::Val::pwrConfAdvPowerSave)) return false;
    delayMicroseconds(600);
    return true;
}

/// Try to suspend the IMU at either possible I2C address (0x68 or 0x69).
/// The BMI270's address depends on the SDO pin wiring, which varies by board
/// revision. We probe chipId at both addresses and suspend whichever responds.
void imuEnterSuspendBestEffort() {
    uint8_t dummy = 0;
    if (readReg8(Imu::addr0, Imu::Reg::chipId, dummy)) { (void)imuEnterSuspendI2c(Imu::addr0); return; }
    if (readReg8(Imu::addr1, Imu::Reg::chipId, dummy)) { (void)imuEnterSuspendI2c(Imu::addr1); return; }
}

// ============================================================================
// Sleep preparation and wake restoration
// ============================================================================

/// Turn off all Expander1-controlled power rails: backlight, LED, Grove, LCD.
/// Called during sleep preparation to minimize quiescent current.
void powerDownUiRailsBestEffort() {
    expander1WriteOutputBit(Expander1::backlightBit, false);
    expander1WriteOutputBit(Expander1::ledBit, false);
    expander1WriteOutputBit(Expander1::grovePowerEnBit, false);
    expander1WriteOutputBit(Expander1::lcdResetBit, false);
}

/// Set all SPI and peripheral GPIOs to high-impedance (INPUT) to prevent
/// parasitic current through floating outputs during deep sleep.
/// Exception: LORA_CS is driven HIGH to keep the SX1262 in idle/standby
/// (CS active LOW would wake it).
void setPinsHiZBestEffort() {
    using namespace mesh::config;
    pinMode(kLoraSck, INPUT);     // SPI clock
    pinMode(kLoraMosi, INPUT);    // SPI MOSI
    pinMode(kLoraMiso, INPUT);    // SPI MISO

    pinMode(LORA_CS, OUTPUT);     // Keep CS deasserted (HIGH) to avoid
    digitalWrite(LORA_CS, HIGH);  // parasitic wakeup of the SX1262

    pinMode(LORA_BUSY, INPUT);    // Radio busy indicator
    pinMode(LORA_IRQ, INPUT);     // Radio interrupt

    pinMode(kBuzzerGpio, INPUT);  // Piezo buzzer
    pinMode(kIrTxGpio, INPUT);    // IR transmitter (unused, Hi-Z for sleep)
}

/// Post-wake initialization: wake the touch controller, enable display,
/// and reconfigure the KEY1 interrupt for future sleep/wake cycles.
void prepareAfterWake() {
    touchWakePulseOnSysIrq();                             // Reset FT6336 out of hibernate
    expander1WriteOutputBit(Expander1::lcdResetBit, true);  // Release LCD from reset
    expander1WriteOutputBit(Expander1::backlightBit, true); // Turn on backlight

    pinMode(static_cast<int>(SYS_IRQ), INPUT_PULLUP);     // Restore SYS_IRQ for IRQ use
    clearExpanderIrqLatch();                               // Clear any stale interrupts
    configureExpanderKey1Interrupt();                       // Re-arm KEY1 wake interrupt
}

/// Full pre-sleep shutdown sequence.
/// 1. Clear any pending IRQ (if button is held during sleep entry, clear the
///    latch so we don't immediately wake on a stale interrupt)
/// 2. Suspend IMU and touch controller
/// 3. Power down display and peripheral rails via IO expander
/// 4. Clear IRQ latch again (IMU/touch shutdown may have generated new events)
/// 5. Set all GPIOs to Hi-Z
/// 6. Disconnect I2C bus (releases SDA/SCL pins for low-power state)
void prepareForSleep() {
    if (isKey1PressedNow() || readSysIrqLevel() == LOW) {
        clearExpanderIrqLatch();
        delay(5);
    }

    imuEnterSuspendBestEffort();
    touchEnterHibernation();
    powerDownUiRailsBestEffort();
    clearExpanderIrqLatch();
    setPinsHiZBestEffort();

    Wire.end();    // Release I2C bus
    delay(20);     // Allow peripheral state to settle
}

}  // anonymous namespace

// ============================================================================
// Public API
// ============================================================================

/// Initialize power subsystem: start I2C, restore peripherals from sleep state,
/// and configure the PMIC for safe LiPo charging.
void init() {
    Wire.begin(SDA, SCL);           // Start I2C bus (needed for expanders, touch, IMU)
    prepareAfterWake();             // Wake touch, enable display, arm KEY1 interrupt

    M5.Power.setBatteryCharge(true);
    M5.Power.setChargeCurrent(config::kChargeCurrent);  // 256mA limit for 180mAh cell
    M5.Power.setChargeVoltage(config::kChargeVoltage);  // 4.2V termination
}

/// Query PMIC for USB power / charging status.
bool isCharging() {
    return M5.Power.isCharging();
}

/// Turn off the status LED. Called before sleep to reduce current draw.
void ledOff() {
    M5.Power.setLed(0);
}

/// Enter ESP32 deep sleep with EXT1 wakeup on KEY1 (SYS_IRQ going LOW).
/// Before sleeping, shuts down all peripherals to minimize quiescent current.
/// On wake, the ESP32 reboots from setup() — execution does not continue here.
[[noreturn]] void enterDeepSleep() {
    const uint64_t wakeMask = 1ULL << static_cast<uint8_t>(SYS_IRQ);
    esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);
    prepareForSleep();
    esp_deep_sleep_start();
    while (true) {}  // Unreachable — satisfies [[noreturn]]
}

/// Tell the AXP2101 PMIC to cut all power rails. The device is fully off
/// until USB is reconnected. Used for the "long press = power off" gesture.
[[noreturn]] void powerOff() {
    M5.Power.powerOff();
    while (true) {}  // Unreachable — satisfies [[noreturn]]
}

}  // namespace mesh::hal::power
