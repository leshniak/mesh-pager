#include <Wire.h>
#include "esp_sleep.h"

#include "DeepSleep.h"

// ============================================================================
// Register maps
// ============================================================================
struct Expander0 {
  static constexpr uint8_t addr = 0x43;  // E0 (buttons)

  // PI4IOE5V6408 registers
  struct Reg {
    static constexpr uint8_t ioDir = 0x03;         // 0=input, 1=output
    static constexpr uint8_t inputDefault = 0x09;  // compare default for IRQ
    static constexpr uint8_t pullEnable = 0x0B;    // 1=enable pull resistor
    static constexpr uint8_t pullSelect = 0x0D;    // 1=pull-up
    static constexpr uint8_t inputStatus = 0x0F;   // current inputs
    static constexpr uint8_t intMask = 0x11;       // 0=IRQ enabled, 1=masked
    static constexpr uint8_t intStatus = 0x13;     // read clears IRQ latch
  };

  // Buttons mapping
  static constexpr uint8_t key1Bit = 0;  // E0.P0, active LOW
};

struct Expander1 {
  static constexpr uint8_t addr = 0x44;  // E1 (power/UI)

  struct Reg {
    static constexpr uint8_t outputPort = 0x01; // OUTPUT PORT (write outputs)
    static constexpr uint8_t ioDir      = 0x03; // 0=input, 1=output (jak u Ciebie)
  };

  // E1 bits (pinout)
  static constexpr uint8_t lcdResetBit     = 1; // E1.P1
  static constexpr uint8_t grovePowerEnBit = 2; // E1.P2
  static constexpr uint8_t backlightBit    = 6; // E1.P6
  static constexpr uint8_t ledBit          = 7; // E1.P7
};

struct Touch {
  static constexpr uint8_t addr = 0x38;  // FT6336U

  struct Reg {
    static constexpr uint8_t tdStatus = 0x02;  // touch points count (read helps clear INT)
    static constexpr uint8_t gMode = 0xA4;     // interrupt mode
    static constexpr uint8_t pwrMode = 0xA5;   // power mode
  };

  struct Val {
    static constexpr uint8_t gModeTrigger = 0x01;
    static constexpr uint8_t pwrHibernate = 0x03;
  };
};

struct Imu {
  // BMI270: zwykle 0x68 albo 0x69
  static constexpr uint8_t addr0 = 0x68;
  static constexpr uint8_t addr1 = 0x69;

  struct Reg {
    static constexpr uint8_t chipId   = 0x00;
    static constexpr uint8_t pwrConf  = 0x7C;
    static constexpr uint8_t pwrCtrl  = 0x7D;
  };

  struct Val {
    // PWR_CTRL: aux_en(0), gyr_en(1), acc_en(2), temp_en(3) => all 0 = off
    static constexpr uint8_t pwrCtrlAllOff = 0x00;
    // PWR_CONF: adv_power_save_en bit0 = 1
    static constexpr uint8_t pwrConfAdvPowerSave = 0x01;
  };
};

// LoRa SPI pins (Nesso N1)
static constexpr uint8_t LORA_SCK  = 21;
static constexpr uint8_t LORA_MISO = 20;
static constexpr uint8_t LORA_MOSI = 22;
// static constexpr uint8_t LORA_CS   = 23;
// static constexpr uint8_t LORA_BUSY = 19;
// static constexpr uint8_t LORA_IRQ  = 15;

// Optional: direct GPIO
static constexpr uint8_t BUZZER_GPIO = 11;
static constexpr uint8_t IR_TX_GPIO  = 9;


// ============================================================================
// I2C helpers (8-bit reg R/W)
// ============================================================================
static bool writeReg8(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool readReg8(uint8_t addr, uint8_t reg, uint8_t &val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)addr, 1) != 1) return false;
  val = (uint8_t)Wire.read();
  return true;
}

static inline int readSysIrqLevel() {
  return digitalRead((int)SYS_IRQ);
}


// ============================================================================
// Expander (buttons) low-level
// ============================================================================
static void clearExpanderIrqLatch() {
  uint8_t dummy = 0;
  (void)readReg8(Expander0::addr, Expander0::Reg::intStatus, dummy);    // releases INT
  (void)readReg8(Expander0::addr, Expander0::Reg::inputStatus, dummy);  // optional
}

static bool isKey1PressedNow() {
  uint8_t inputs = 0;
  if (!readReg8(Expander0::addr, Expander0::Reg::inputStatus, inputs)) return false;
  return ((inputs & (1u << Expander0::key1Bit)) == 0);  // active LOW
}

static void configureExpanderKey1Interrupt() {
  uint8_t v = 0;

  // P0 as input (bit=0)
  if (readReg8(Expander0::addr, Expander0::Reg::ioDir, v)) {
    v &= ~(1u << Expander0::key1Bit);
    (void)writeReg8(Expander0::addr, Expander0::Reg::ioDir, v);
  } else {
    (void)writeReg8(Expander0::addr, Expander0::Reg::ioDir, 0x00);  // fallback: all inputs
  }

  // Enable pull on P0
  if (readReg8(Expander0::addr, Expander0::Reg::pullEnable, v)) {
    v |= (1u << Expander0::key1Bit);
    (void)writeReg8(Expander0::addr, Expander0::Reg::pullEnable, v);
  }

  // Pull-up on P0 (active LOW)
  if (readReg8(Expander0::addr, Expander0::Reg::pullSelect, v)) {
    v |= (1u << Expander0::key1Bit);
    (void)writeReg8(Expander0::addr, Expander0::Reg::pullSelect, v);
  }

  // Default compare HIGH -> IRQ when it goes LOW
  if (readReg8(Expander0::addr, Expander0::Reg::inputDefault, v)) {
    v |= (1u << Expander0::key1Bit);
    (void)writeReg8(Expander0::addr, Expander0::Reg::inputDefault, v);
  }

  // Unmask only P0 (0 = enabled)
  uint8_t mask = 0xFF;
  mask &= ~(1u << Expander0::key1Bit);
  (void)writeReg8(Expander0::addr, Expander0::Reg::intMask, mask);

  clearExpanderIrqLatch();
}

static void expander1WriteOutputBit(uint8_t bit, bool levelHigh) {
  uint8_t dir = 0, out = 0;

  (void)readReg8(Expander1::addr, Expander1::Reg::ioDir, dir);
  (void)readReg8(Expander1::addr, Expander1::Reg::outputPort, out);

  // ustaw bit jako output (1=output wg Twojego komentarza)
  dir |= (1u << bit);

  if (levelHigh) out |=  (1u << bit);
  else           out &= ~(1u << bit);

  (void)writeReg8(Expander1::addr, Expander1::Reg::ioDir, dir);
  (void)writeReg8(Expander1::addr, Expander1::Reg::outputPort, out);
}

// ============================================================================
// Touch (FT6336U) low-level
// ============================================================================
static void clearTouchIrqBestEffort() {
  uint8_t v = 0;
  (void)readReg8(Touch::addr, Touch::Reg::tdStatus, v);
}

static void touchEnterHibernation() {
  (void)writeReg8(Touch::addr, Touch::Reg::gMode, Touch::Val::gModeTrigger);
  clearTouchIrqBestEffort();
  (void)writeReg8(Touch::addr, Touch::Reg::pwrMode, Touch::Val::pwrHibernate);
  delay(5);
}

static void touchWakePulseOnSysIrq() {
  // Shared open-drain line: drive LOW briefly, then release to pull-up.
  pinMode((int)SYS_IRQ, OUTPUT);
  digitalWrite((int)SYS_IRQ, LOW);
  delay(1);
  pinMode((int)SYS_IRQ, INPUT_PULLUP);
  delay(5);
}

// ============================================================================
// IMU (BMI270) low-level
// ============================================================================
static bool imuEnterSuspendI2c(uint8_t addr) {
  // Disable accel/gyro/aux/temp
  if (!writeReg8(addr, Imu::Reg::pwrCtrl, Imu::Val::pwrCtrlAllOff)) return false;

  delayMicroseconds(600); // datasheet timing (>=450us) before enabling adv power save
  if (!writeReg8(addr, Imu::Reg::pwrConf, Imu::Val::pwrConfAdvPowerSave)) return false;

  delayMicroseconds(600);
  return true;
}

static void imuEnterSuspendBestEffort() {
  uint8_t dummy = 0;
  if (readReg8(Imu::addr0, Imu::Reg::chipId, dummy)) { (void)imuEnterSuspendI2c(Imu::addr0); return; }
  if (readReg8(Imu::addr1, Imu::Reg::chipId, dummy)) { (void)imuEnterSuspendI2c(Imu::addr1); return; }
}

// ============================================================================
// "Best-effort" helpers
// ============================================================================
static void powerDownUiRailsBestEffort() {
  // If backlight/LED are already handled elsewhere, you can omit these.
  expander1WriteOutputBit(Expander1::backlightBit, false);
  expander1WriteOutputBit(Expander1::ledBit, false);

  expander1WriteOutputBit(Expander1::grovePowerEnBit, false);

  // Optional: keep the LCD held in reset (often shaves a bit more current).
  expander1WriteOutputBit(Expander1::lcdResetBit, false);
}

static void setPinsHiZBestEffort() {
  // Note: SYS_IRQ must stay INPUT_PULLUP (wake), so we don't touch it here.

  // LoRa lines: setting them to hi-Z can reduce leakage through IO pins
  pinMode(LORA_SCK,  INPUT);
  pinMode(LORA_MOSI, INPUT);
  pinMode(LORA_MISO, INPUT);

  // CS: you can leave it as OUTPUT HIGH (safe), or release to hi-Z.
  // Safest: keep OUTPUT HIGH.
  pinMode(LORA_CS, OUTPUT);
  digitalWrite(LORA_CS, HIGH);

  pinMode(LORA_BUSY, INPUT);
  pinMode(LORA_IRQ,  INPUT);

  // Buzzer / IR: hi-Z
  pinMode(BUZZER_GPIO, INPUT);
  pinMode(IR_TX_GPIO,  INPUT);
}


// ============================================================================
// "Flow" helpers (readable lifecycle)
// ============================================================================
static void prepareAfterWake() {
  // If touch was hibernated before, wake it early so UI works after reboot/wake.
  touchWakePulseOnSysIrq();
  expander1WriteOutputBit(Expander1::lcdResetBit, true);
  expander1WriteOutputBit(Expander1::backlightBit, true);

  // Shared IRQ line must idle HIGH (pull-up), sources are open-drain.
  pinMode((int)SYS_IRQ, INPUT_PULLUP);

  // Release any latched expander IRQ from previous run.
  clearExpanderIrqLatch();

  // Configure KEY1 to be the only expander source that can pull SYS_IRQ low.
  configureExpanderKey1Interrupt();
}

static void prepareForSleep() {
  // Avoid immediate wake if IRQ already low or key held.
  if (isKey1PressedNow() || readSysIrqLevel() == LOW) {
    clearExpanderIrqLatch();
    delay(5);
  }

  imuEnterSuspendBestEffort();
  // Stop touch from asserting the shared IRQ line during sleep.
  touchEnterHibernation();
  powerDownUiRailsBestEffort();
  // Ensure the shared line is released right before sleep.
  clearExpanderIrqLatch();
  setPinsHiZBestEffort();
  

  Wire.end();
  delay(20);
}

// ============================================================================
// Public API (as in your original DeepSleep.h)
// ============================================================================
bool setupDeepSleep() {
  Wire.begin(SDA, SCL);
  prepareAfterWake();
  return true;  // best-effort
}

[[noreturn]] void enterDeepSleep() {
  // Configure wake: SYS_IRQ low (EXT1)
  const uint64_t wakeMask = 1ULL << (uint8_t)SYS_IRQ;
  esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);

  prepareForSleep();

  esp_deep_sleep_start();
  while (true) { /* noreturn */
  }
}
