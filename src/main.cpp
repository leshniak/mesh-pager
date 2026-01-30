#include <M5Unified.h>
#include <WiFi.h>
#include <Preferences.h>

#include "MeshRadio.h"
#include "DeepSleep.h"
#include "secrets.h"

#define APP_NAME "mesh-remote"
#define MESSAGE_INDEX "messageIndex"

static const size_t messagesSize = sizeof(messages) / sizeof(*messages);

static constexpr uint8_t LORA_ENABLE_PIN = GPIO_NUM_7;
static constexpr uint8_t LORA_LNA_ENABLE_PIN = GPIO_NUM_5;
static constexpr uint8_t LORA_ANTENNA_SWITCH_PIN = GPIO_NUM_6;

static MeshRadio meshRadio;
static Preferences preferences;
static uint8_t messageIdx;

static void onTextReceived(uint32_t fromNodeId, const char* text) {
  M5.Log(ESP_LOG_INFO, ">> <!%08X>\n%s\n", fromNodeId, text);
  M5.Speaker.tone(6000, 50);
}

static void setupM5Logging() {
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

static void setupLoRaPowerAndRfSwitches() {
  auto& ioExpander = M5.getIOExpander(0);

  // LoRa reset + RF switches
  ioExpander.digitalWrite(LORA_ENABLE_PIN, false);
  delay(100);
  ioExpander.digitalWrite(LORA_ENABLE_PIN, true);
  delay(100);

  ioExpander.digitalWrite(LORA_LNA_ENABLE_PIN, true);      // LORA_LNA_ENABLE
  ioExpander.digitalWrite(LORA_ANTENNA_SWITCH_PIN, true);  // LORA_ANTENNA_SWITCH
}

static void sleepLoRa() {
  auto& ioExpander = M5.getIOExpander(0);

  ioExpander.digitalWrite(LORA_ENABLE_PIN, false);
  ioExpander.digitalWrite(LORA_LNA_ENABLE_PIN, false);
  ioExpander.digitalWrite(LORA_ANTENNA_SWITCH_PIN, false);
}

static void savePreferences() {
  preferences.begin(APP_NAME, false);

  if (messageIdx != preferences.getUChar(MESSAGE_INDEX)) {
    preferences.putUChar(MESSAGE_INDEX, messageIdx);
  }

  preferences.end();
}

static void sleep() {
  M5.Display.sleep();
  M5.Display.waitDisplay();

  if (M5.Power.isCharging()) {
    return;
  }

  savePreferences();
  meshRadio.sleep();
  sleepLoRa();
  M5.Power.setLed(0);

  M5.Speaker.tone(2500, 100);
  delay(150);
  M5.Speaker.tone(2500, 100);
  delay(150);

  enterDeepSleep();
}

static void powerOff() {
  savePreferences();
  meshRadio.sleep();

  M5.Speaker.tone(6000, 100);
  delay(150);
  M5.Speaker.tone(4000, 100);
  delay(150);
  M5.Speaker.tone(2500, 100);
  delay(150);

  M5.Power.powerOff();
}

void setup() {
  auto config = M5.config();
  config.serial_baudrate = 115200;
  M5.begin(config);

  M5.Imu.sleep();
  M5.Power.setExtOutput(false);

  setupDeepSleep();

  preferences.begin(APP_NAME, true);
  messageIdx = preferences.getUChar(MESSAGE_INDEX, 0);
  preferences.end();

  if (messageIdx >= messagesSize) {
    messageIdx = 0;
  }

  M5.Power.setBatteryCharge(true);
  M5.Power.setChargeCurrent(256);
  M5.Power.setChargeVoltage(4200);

  setupM5Logging();
  setupLoRaPowerAndRfSwitches();

  MeshRadio::Config meshConfig;
  meshConfig.channelName = channelName;
  meshConfig.channelKeyBase64 = channelKey;

  WiFi.mode(WIFI_STA);
  WiFi.macAddress(meshConfig.macAddress);
  WiFi.mode(WIFI_OFF);

  // LoRa params (must match your Meshtastic preset)
  meshConfig.frequencyMHz = 869.525F;
  meshConfig.bandwidthKHz = 250.0F;
  meshConfig.spreadingFactor = 9;
  meshConfig.codingRate = 5;
  meshConfig.syncWord = 0x2B;
  meshConfig.preambleLength = 16;
  meshConfig.tcxoVoltage = 1.6F;

  // Max TX power
  meshConfig.outputPowerDbm = 22;
  meshConfig.currentLimitmA = 140.0F;

  // Mesh flags
  meshConfig.hopLimit = 3;
  meshConfig.wantAck = false;
  meshConfig.viaMqtt = false;
  meshConfig.hopStart = 3;

  meshRadio.setTextRxCallback(onTextReceived);

  if (!meshRadio.begin(meshConfig)) {
    M5.Log(ESP_LOG_ERROR, "meshRadio.begin failed err=%d", meshRadio.getLastError());
    while (1) delay(1000);
  }

  M5.Log(ESP_LOG_INFO, "NodeId:  !%08X", meshRadio.getNodeId());
  M5.Log(ESP_LOG_INFO, "Channel: %s", channelName);
  M5.Log(ESP_LOG_INFO, "Battery: %d%%", M5.Power.getBatteryLevel());
  M5.Log(ESP_LOG_INFO, "Canned message: %s", messages[messageIdx]);
  M5.Log(ESP_LOG_INFO, "");
}

void loop() {
  M5.update();
  meshRadio.pollRx();

  static uint32_t lastActionMs = 0;
  const uint32_t nowMs = millis();
  static bool wasCharging = M5.Power.isCharging();
  const bool isCharging = M5.Power.isCharging();

  if (wasCharging != isCharging) {
    wasCharging = isCharging;
    lastActionMs = nowMs;
    M5.Display.wakeup();
  }

  if (M5.Touch.getCount() > 0) {
    M5.Display.wakeup();
    lastActionMs = nowMs;
  }

  if (M5.BtnA.pressedFor(1000)) {
    powerOff();
    return;
  }

  if (nowMs - lastActionMs >= 15000) {
    sleep();
    return;
  }

  if (M5.BtnA.wasDoubleClicked()) {
    lastActionMs = nowMs;
    messageIdx += 1;

    if (messageIdx >= messagesSize) {
      messageIdx = 0;
    }

    M5.Log(ESP_LOG_INFO, "Canned message:\n%s\n", messages[messageIdx]);
  } else if (M5.BtnA.wasSingleClicked() && (nowMs - lastActionMs) > 1000) {
    lastActionMs = nowMs;

    const char* message = messages[messageIdx];
    const bool ok = meshRadio.txText(message);

    if (ok) {
      M5.Log(ESP_LOG_INFO, "<< <!%08X>\n%s\n", meshRadio.getNodeId(), message);
      M5.Speaker.tone(4000, 50);
    } else {
      M5.Log(ESP_LOG_ERROR, "TX failed err=%d", meshRadio.getLastError());
    }
  }
}
