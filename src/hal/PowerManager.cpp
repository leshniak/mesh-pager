#include "hal/PowerManager.h"
#include "config/Pins.h"
#include "config/AppConfig.h"

#include <M5Unified.h>
#include <Wire.h>
#include <esp_sleep.h>

namespace mesh::hal::power {

// ============================================================================
// Register maps (Nesso N1 hardware)
// ============================================================================

namespace {

struct Expander0 {
    static constexpr uint8_t addr = 0x43;
    struct Reg {
        static constexpr uint8_t ioDir        = 0x03;
        static constexpr uint8_t inputDefault = 0x09;
        static constexpr uint8_t pullEnable   = 0x0B;
        static constexpr uint8_t pullSelect   = 0x0D;
        static constexpr uint8_t inputStatus  = 0x0F;
        static constexpr uint8_t intMask      = 0x11;
        static constexpr uint8_t intStatus    = 0x13;
    };
    static constexpr uint8_t key1Bit = 0;
};

struct Expander1 {
    static constexpr uint8_t addr = 0x44;
    struct Reg {
        static constexpr uint8_t outputPort = 0x01;
        static constexpr uint8_t ioDir      = 0x03;
    };
    static constexpr uint8_t lcdResetBit     = 1;
    static constexpr uint8_t grovePowerEnBit = 2;
    static constexpr uint8_t backlightBit    = 6;
    static constexpr uint8_t ledBit          = 7;
};

struct Touch {
    static constexpr uint8_t addr = 0x38;
    struct Reg {
        static constexpr uint8_t tdStatus = 0x02;
        static constexpr uint8_t gMode    = 0xA4;
        static constexpr uint8_t pwrMode  = 0xA5;
    };
    struct Val {
        static constexpr uint8_t gModeTrigger = 0x01;
        static constexpr uint8_t pwrHibernate = 0x03;
    };
};

struct Imu {
    static constexpr uint8_t addr0 = 0x68;
    static constexpr uint8_t addr1 = 0x69;
    struct Reg {
        static constexpr uint8_t chipId  = 0x00;
        static constexpr uint8_t pwrConf = 0x7C;
        static constexpr uint8_t pwrCtrl = 0x7D;
    };
    struct Val {
        static constexpr uint8_t pwrCtrlAllOff      = 0x00;
        static constexpr uint8_t pwrConfAdvPowerSave = 0x01;
    };
};

// ============================================================================
// I2C helpers
// ============================================================================

bool writeReg8(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

bool readReg8(uint8_t addr, uint8_t reg, uint8_t& val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(static_cast<int>(addr), 1) != 1) return false;
    val = static_cast<uint8_t>(Wire.read());
    return true;
}

int readSysIrqLevel() {
    return digitalRead(static_cast<int>(SYS_IRQ));
}

// ============================================================================
// Expander helpers
// ============================================================================

void clearExpanderIrqLatch() {
    uint8_t dummy = 0;
    (void)readReg8(Expander0::addr, Expander0::Reg::intStatus, dummy);
    (void)readReg8(Expander0::addr, Expander0::Reg::inputStatus, dummy);
}

bool isKey1PressedNow() {
    uint8_t inputs = 0;
    if (!readReg8(Expander0::addr, Expander0::Reg::inputStatus, inputs)) return false;
    return (inputs & (1u << Expander0::key1Bit)) == 0;  // active LOW
}

void configureExpanderKey1Interrupt() {
    uint8_t v = 0;

    if (readReg8(Expander0::addr, Expander0::Reg::ioDir, v)) {
        v &= ~(1u << Expander0::key1Bit);
        (void)writeReg8(Expander0::addr, Expander0::Reg::ioDir, v);
    } else {
        (void)writeReg8(Expander0::addr, Expander0::Reg::ioDir, 0x00);
    }

    if (readReg8(Expander0::addr, Expander0::Reg::pullEnable, v)) {
        v |= (1u << Expander0::key1Bit);
        (void)writeReg8(Expander0::addr, Expander0::Reg::pullEnable, v);
    }

    if (readReg8(Expander0::addr, Expander0::Reg::pullSelect, v)) {
        v |= (1u << Expander0::key1Bit);
        (void)writeReg8(Expander0::addr, Expander0::Reg::pullSelect, v);
    }

    if (readReg8(Expander0::addr, Expander0::Reg::inputDefault, v)) {
        v |= (1u << Expander0::key1Bit);
        (void)writeReg8(Expander0::addr, Expander0::Reg::inputDefault, v);
    }

    uint8_t mask = 0xFF;
    mask &= ~(1u << Expander0::key1Bit);
    (void)writeReg8(Expander0::addr, Expander0::Reg::intMask, mask);

    clearExpanderIrqLatch();
}

void expander1WriteOutputBit(uint8_t bit, bool high) {
    uint8_t dir = 0, out = 0;
    (void)readReg8(Expander1::addr, Expander1::Reg::ioDir, dir);
    (void)readReg8(Expander1::addr, Expander1::Reg::outputPort, out);

    dir |= (1u << bit);
    if (high) out |= (1u << bit);
    else      out &= ~(1u << bit);

    (void)writeReg8(Expander1::addr, Expander1::Reg::ioDir, dir);
    (void)writeReg8(Expander1::addr, Expander1::Reg::outputPort, out);
}

// ============================================================================
// Touch / IMU helpers
// ============================================================================

void clearTouchIrqBestEffort() {
    uint8_t v = 0;
    (void)readReg8(Touch::addr, Touch::Reg::tdStatus, v);
}

void touchEnterHibernation() {
    (void)writeReg8(Touch::addr, Touch::Reg::gMode, Touch::Val::gModeTrigger);
    clearTouchIrqBestEffort();
    (void)writeReg8(Touch::addr, Touch::Reg::pwrMode, Touch::Val::pwrHibernate);
    delay(5);
}

void touchWakePulseOnSysIrq() {
    pinMode(static_cast<int>(SYS_IRQ), OUTPUT);
    digitalWrite(static_cast<int>(SYS_IRQ), LOW);
    delay(1);
    pinMode(static_cast<int>(SYS_IRQ), INPUT_PULLUP);
    delay(5);
}

bool imuEnterSuspendI2c(uint8_t addr) {
    if (!writeReg8(addr, Imu::Reg::pwrCtrl, Imu::Val::pwrCtrlAllOff)) return false;
    delayMicroseconds(600);
    if (!writeReg8(addr, Imu::Reg::pwrConf, Imu::Val::pwrConfAdvPowerSave)) return false;
    delayMicroseconds(600);
    return true;
}

void imuEnterSuspendBestEffort() {
    uint8_t dummy = 0;
    if (readReg8(Imu::addr0, Imu::Reg::chipId, dummy)) { (void)imuEnterSuspendI2c(Imu::addr0); return; }
    if (readReg8(Imu::addr1, Imu::Reg::chipId, dummy)) { (void)imuEnterSuspendI2c(Imu::addr1); return; }
}

// ============================================================================
// Sleep preparation
// ============================================================================

void powerDownUiRailsBestEffort() {
    expander1WriteOutputBit(Expander1::backlightBit, false);
    expander1WriteOutputBit(Expander1::ledBit, false);
    expander1WriteOutputBit(Expander1::grovePowerEnBit, false);
    expander1WriteOutputBit(Expander1::lcdResetBit, false);
}

void setPinsHiZBestEffort() {
    using namespace mesh::config;
    pinMode(kLoraSck, INPUT);
    pinMode(kLoraMosi, INPUT);
    pinMode(kLoraMiso, INPUT);

    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);

    pinMode(LORA_BUSY, INPUT);
    pinMode(LORA_IRQ, INPUT);

    pinMode(kBuzzerGpio, INPUT);
    pinMode(kIrTxGpio, INPUT);
}

void prepareAfterWake() {
    touchWakePulseOnSysIrq();
    expander1WriteOutputBit(Expander1::lcdResetBit, true);
    expander1WriteOutputBit(Expander1::backlightBit, true);

    pinMode(static_cast<int>(SYS_IRQ), INPUT_PULLUP);
    clearExpanderIrqLatch();
    configureExpanderKey1Interrupt();
}

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

    Wire.end();
    delay(20);
}

}  // anonymous namespace

// ============================================================================
// Public API
// ============================================================================

void init() {
    Wire.begin(SDA, SCL);
    prepareAfterWake();

    M5.Power.setBatteryCharge(true);
    M5.Power.setChargeCurrent(config::kChargeCurrent);
    M5.Power.setChargeVoltage(config::kChargeVoltage);
}

bool isCharging() {
    return M5.Power.isCharging();
}

void ledOff() {
    M5.Power.setLed(0);
}

[[noreturn]] void enterDeepSleep() {
    const uint64_t wakeMask = 1ULL << static_cast<uint8_t>(SYS_IRQ);
    esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);
    prepareForSleep();
    esp_deep_sleep_start();
    while (true) {}
}

[[noreturn]] void powerOff() {
    M5.Power.powerOff();
    while (true) {}
}

}  // namespace mesh::hal::power
