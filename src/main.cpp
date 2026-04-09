#include <M5Unified.h>
#include <WiFi.h>

#include "config/AppConfig.h"
#include "config/Pins.h"
#include "protocol/MeshTypes.h"
#include "protocol/MeshCodec.h"
#include "protocol/MeshPacket.h"
#include "hal/RadioHal.h"
#include "hal/PowerManager.h"
#include "hal/Buzzer.h"
#include "hal/Display.h"
#include "app/AppState.h"
#include "app/CannedMessages.h"
#include "secrets.h"

namespace {

using namespace mesh;

// Module instances
hal::RadioHal radioHal;
app::CannedMessages cannedMessages;

// Mesh identity
uint8_t channelKeyBytes[protocol::kKeyLen] = {};
uint8_t channelHashByte = 0;
protocol::NodeId nodeId = 0;
uint32_t packetIdCounter = 0;

// State
app::State appState = app::State::Idle;
uint32_t lastActionMs = 0;
bool wasCharging = false;
bool pendingRx = false;
uint8_t rxFrameBuf[protocol::kRxBufferLen] = {};
size_t rxFrameLen = 0;

void onRadioRx(const uint8_t* data, size_t len) {
    if (len <= protocol::kRxBufferLen) {
        memcpy(rxFrameBuf, data, len);
        rxFrameLen = len;
        pendingRx = true;
    }
}

void handleTransmit() {
    auto text = cannedMessages.current();
    if (text.empty()) return;

    // Encode protobuf payload
    uint8_t payload[4 + protocol::kMaxTextLen];
    const size_t payloadLen = protocol::encodeTextPayload(
        reinterpret_cast<const uint8_t*>(text.data()), text.size(),
        payload, sizeof(payload));
    if (payloadLen == 0) {
        hal::display::logError("TX encode failed");
        return;
    }

    // Build packet header
    protocol::PacketHeader hdr;
    hdr.dest = protocol::kBroadcastAddr;
    hdr.source = nodeId;
    hdr.packetId = packetIdCounter++;
    hdr.flags = protocol::makeMeshFlags(
        config::kHopLimit, config::kWantAck, config::kViaMqtt, config::kHopStart);
    hdr.channelHash = channelHashByte;

    // Build and transmit frame
    uint8_t frame[protocol::kMeshHeaderLen + sizeof(payload)];
    const size_t frameLen = protocol::buildPacket(
        hdr, payload, payloadLen, channelKeyBytes, frame, sizeof(frame));
    if (frameLen == 0) {
        hal::display::logError("TX build failed");
        return;
    }

    if (radioHal.transmit(frame, frameLen) == protocol::MeshError::Ok) {
        hal::display::logInfo("<< <!%08X>\n%s\n", nodeId, text.data());
        hal::playTxTone();
    } else {
        hal::display::logError("TX failed");
    }
}

void handleReceive() {
    if (!pendingRx) return;
    pendingRx = false;

    protocol::PacketHeader hdr;
    char textOut[200];
    size_t textLen = 0;

    auto err = protocol::parsePacket(
        rxFrameBuf, rxFrameLen, channelKeyBytes,
        hdr, textOut, sizeof(textOut), textLen);

    if (err != protocol::MeshError::Ok) return;
    if (hdr.channelHash != channelHashByte) return;
    if (hdr.source == nodeId) return;
    if (textLen == 0) return;

    hal::display::logInfo(">> <!%08X>\n%s\n", hdr.source, textOut);
    hal::playRxTone();
}

void handleSleep() {
    hal::display::sleep();

    if (hal::power::isCharging()) return;

    cannedMessages.save();
    radioHal.sleep();
    hal::power::ledOff();
    hal::playSleepTone();
    hal::power::enterDeepSleep();
}

void handlePowerOff() {
    cannedMessages.save();
    radioHal.sleep();
    hal::playPowerOffTone();
    hal::power::powerOff();
}

}  // anonymous namespace

void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = config::kSerialBaudRate;
    M5.begin(cfg);

    M5.Imu.sleep();
    M5.Power.setExtOutput(false);

    hal::power::init();
    hal::display::init();

    // Initialize canned messages
    constexpr size_t msgCount = sizeof(messages) / sizeof(messages[0]);
    cannedMessages.init(messages, msgCount);

    // Derive mesh identity from MAC
    uint8_t mac[6] = {};
    WiFi.mode(WIFI_STA);
    WiFi.macAddress(mac);
    WiFi.mode(WIFI_OFF);

    nodeId = protocol::nodeIdFromMac(mac);
    randomSeed(static_cast<uint32_t>(esp_random()));
    packetIdCounter = static_cast<uint32_t>(random(0x1000, 0xFFFF));

    // Decode channel key and compute hash
    if (!protocol::decodeBase64Key(channelKey, channelKeyBytes)) {
        hal::display::logError("Channel key decode failed");
        while (true) delay(1000);
    }
    channelHashByte = protocol::computeChannelHash(channelName, channelKeyBytes);

    // Initialize radio
    radioHal.setRxCallback(onRadioRx);
    if (radioHal.begin() != protocol::MeshError::Ok) {
        hal::display::logError("Radio init failed");
        while (true) delay(1000);
    }

    hal::display::logInfo("NodeId:  !%08X", nodeId);
    hal::display::logInfo("Channel: %s", channelName);
    hal::display::logInfo("Battery: %d%%", M5.Power.getBatteryLevel());
    hal::display::logInfo("Canned message: %s", cannedMessages.current().data());
    hal::display::logInfo("");
}

void loop() {
    M5.update();
    radioHal.pollRx();

    const uint32_t now = millis();
    const bool charging = hal::power::isCharging();

    // Reset activity timer on charge state change or touch
    if (wasCharging != charging) {
        wasCharging = charging;
        lastActionMs = now;
        hal::display::wakeup();
    }
    if (M5.Touch.getCount() > 0) {
        hal::display::wakeup();
        lastActionMs = now;
    }

    // Gather input events
    app::InputEvents events;
    events.singleClick   = M5.BtnA.wasSingleClicked();
    events.doubleClick   = M5.BtnA.wasDoubleClicked();
    events.longPress     = M5.BtnA.pressedFor(config::kButtonHoldMs);
    events.touchActive   = M5.Touch.getCount() > 0;
    events.isCharging    = charging;
    events.chargingChanged = (wasCharging != charging);
    events.rxReady       = pendingRx;
    events.timeSinceLastActionMs = now - lastActionMs;

    // State machine
    appState = app::nextState(appState, events);

    switch (appState) {
        case app::State::Transmitting:
            lastActionMs = now;
            handleTransmit();
            appState = app::State::Idle;
            break;

        case app::State::Receiving:
            handleReceive();
            appState = app::State::Idle;
            break;

        case app::State::EnteringSleep:
            handleSleep();
            appState = app::State::Idle;  // only reached if charging
            break;

        case app::State::PoweringOff:
            handlePowerOff();
            break;  // never reached

        case app::State::Idle:
            if (events.doubleClick) {
                lastActionMs = now;
                cannedMessages.next();
                hal::display::logInfo("Canned message:\n%s\n", cannedMessages.current().data());
            }
            break;
    }
}
