#include <M5Unified.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include "config/AppConfig.h"
#include "config/Pins.h"
#include "config/UIConfig.h"
#include "protocol/MeshTypes.h"
#include "protocol/MeshCodec.h"
#include "protocol/MeshPacket.h"
#include "hal/RadioHal.h"
#include "hal/PowerManager.h"
#include "hal/Buzzer.h"
#include "hal/Display.h"
#include "hal/TouchInput.h"
#include "app/AppState.h"
#include "app/CannedMessages.h"
#include "ui/Renderer.h"
#include "ui/ToastManager.h"
#include "secrets.h"

namespace {

using namespace mesh;

// Module instances
hal::RadioHal radioHal;
hal::TouchInput touchInput;
app::CannedMessages cannedMessages;
ui::Renderer renderer;
ui::ToastManager toastManager;

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

// UI state
bool dirty = true;
bool displaySleeping = false;
bool displayDimmed = false;
bool showHistory = false;
bool showHint = true;

// Hold state
bool isHolding = false;
float holdProgress = 0.0f;

// Sent flash
bool sentFlash = false;
bool errorFlash = false;
uint32_t flashStartMs = 0;

void onRadioRx(const uint8_t* data, size_t len) {
    if (len <= protocol::kRxBufferLen) {
        memcpy(rxFrameBuf, data, len);
        rxFrameLen = len;
        pendingRx = true;
    }
}

bool handleTransmit(uint32_t now) {
    auto text = cannedMessages.current();
    if (text.empty()) return false;

    uint8_t payload[4 + protocol::kMaxTextLen];
    const size_t payloadLen = protocol::encodeTextPayload(
        reinterpret_cast<const uint8_t*>(text.data()), text.size(),
        payload, sizeof(payload));
    if (payloadLen == 0) {
        hal::display::logError("TX encode failed");
        return false;
    }

    protocol::PacketHeader hdr;
    hdr.dest = protocol::kBroadcastAddr;
    hdr.source = nodeId;
    hdr.packetId = packetIdCounter++;
    hdr.flags = protocol::makeMeshFlags(
        config::kHopLimit, config::kWantAck, config::kViaMqtt, config::kHopStart);
    hdr.channelHash = channelHashByte;

    uint8_t frame[protocol::kMeshHeaderLen + sizeof(payload)];
    const size_t frameLen = protocol::buildPacket(
        hdr, payload, payloadLen, channelKeyBytes, frame, sizeof(frame));
    if (frameLen == 0) {
        hal::display::logError("TX build failed");
        return false;
    }

    if (radioHal.transmit(frame, frameLen) == protocol::MeshError::Ok) {
        hal::display::logInfo("<< <!%08X> %s", nodeId, text.data());
        hal::playTxTone();
        toastManager.addMessage(nodeId, text.data(), now);
        return true;
    }

    hal::display::logError("TX failed");
    return false;
}

void handleReceive(uint32_t now) {
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

    hal::display::logInfo(">> <!%08X> %s", hdr.source, textOut);
    hal::playRxTone();
    toastManager.addMessage(hdr.source, textOut, now);
    dirty = true;
}

void handleSleep() {
    displaySleeping = true;
    displayDimmed = false;
    hal::display::sleep();
    touchInput.consumeNextTouch();

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

void renderFrame(uint32_t now) {
    ui::RenderState state;
    state.channelName = channelName;
    state.nodeId = nodeId;
    state.batteryPercent = static_cast<uint8_t>(M5.Power.getBatteryLevel());
    state.messageText = cannedMessages.current().data();
    state.messageIndex = cannedMessages.index();
    state.messageCount = static_cast<uint8_t>(cannedMessages.count());
    state.holdProgress = holdProgress;
    state.isHolding = isHolding;
    state.sentFlash = sentFlash;
    state.errorFlash = errorFlash;
    state.showHistory = showHistory;
    state.showHint = showHint;
    state.toastManager = &toastManager;
    state.nowMs = now;

    renderer.render(state);
}

}  // anonymous namespace

void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = config::kSerialBaudRate;
    M5.begin(cfg);

    M5.Imu.sleep();
    M5.Power.setExtOutput(false);
    setCpuFrequencyMhz(config::kCpuFreqMHz);  // 80MHz saves ~25% power vs 160MHz

    hal::power::init();
    hal::display::init();
    touchInput.init();
    renderer.init();

    constexpr size_t msgCount = sizeof(messages) / sizeof(messages[0]);
    cannedMessages.init(messages, msgCount);

    uint8_t mac[6] = {};
    WiFi.mode(WIFI_STA);
    WiFi.macAddress(mac);
    WiFi.mode(WIFI_OFF);

    nodeId = protocol::nodeIdFromMac(mac);
    randomSeed(static_cast<uint32_t>(esp_random()));
    packetIdCounter = static_cast<uint32_t>(random(0x1000, 0xFFFF));

    if (!protocol::decodeBase64Key(channelKey, channelKeyBytes)) {
        hal::display::logError("Channel key decode failed");
        while (true) delay(1000);
    }
    channelHashByte = protocol::computeChannelHash(channelName, channelKeyBytes);

    radioHal.setRxCallback(onRadioRx);
    if (radioHal.begin() != protocol::MeshError::Ok) {
        hal::display::logError("Radio init failed");
        while (true) delay(1000);
    }

    hal::display::logInfo("NodeId: !%08X", nodeId);
    hal::display::logInfo("Channel: %s", channelName);
    hal::display::logInfo("Battery: %d%%", M5.Power.getBatteryLevel());

    lastActionMs = millis();

    // Send-on-wake: auto-transmit when waking from deep sleep
    if constexpr (config::kSendOnWake) {
        if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED) {
            const uint32_t now = millis();
            if (handleTransmit(now)) {
                sentFlash = true;
            } else {
                errorFlash = true;
            }
            flashStartMs = now;
            dirty = true;
        }
    }
}

void loop() {
    M5.update();
    radioHal.pollRx();

    const uint32_t now = millis();
    const bool charging = hal::power::isCharging();

    // Detect charge state change
    const bool chargingChanged = (wasCharging != charging);
    if (chargingChanged) {
        wasCharging = charging;
        lastActionMs = now;
        if (displaySleeping) {
            displaySleeping = false;
            hal::display::wakeup();
            touchInput.consumeNextTouch();
        }
        dirty = true;
    }

    // Process touch input
    auto touchEvent = touchInput.update();

    if (touchEvent.touching) {
        lastActionMs = now;

        if (displaySleeping && touchEvent.gesture == hal::TouchGesture::Wake) {
            displaySleeping = false;
            displayDimmed = false;
            hal::display::wakeup();
            dirty = true;
        } else if (displayDimmed) {
            displayDimmed = false;
            hal::display::brighten();
        }
    }

    // Handle touch gestures (only when display is awake)
    if (!displaySleeping) {
        switch (touchEvent.gesture) {
            case hal::TouchGesture::SwipeLeft:
                cannedMessages.previous();
                showHint = false;
                isHolding = false;
                holdProgress = 0.0f;
                dirty = true;
                break;

            case hal::TouchGesture::SwipeRight:
                cannedMessages.next();
                showHint = false;
                isHolding = false;
                holdProgress = 0.0f;
                dirty = true;
                break;

            case hal::TouchGesture::HoldTick:
                if (!showHistory) {
                    isHolding = true;
                    holdProgress = touchEvent.holdProgress;
                    showHint = false;
                    dirty = true;
                }
                break;

            case hal::TouchGesture::HoldComplete:
                isHolding = false;
                holdProgress = 0.0f;
                if (!showHistory) {
                    showHint = false;
                    dirty = true;
                }
                break;

            case hal::TouchGesture::SwipeDown:
                if (!showHistory) {
                    showHistory = true;
                    dirty = true;
                }
                break;

            case hal::TouchGesture::SwipeUp:
                if (showHistory) {
                    showHistory = false;
                    dirty = true;
                }
                break;

            case hal::TouchGesture::None:
                if (!touchEvent.touching && isHolding) {
                    isHolding = false;
                    holdProgress = 0.0f;
                    dirty = true;
                }
                break;

            default:
                break;
        }

    }

    // Update toast manager
    if (toastManager.update(now)) {
        dirty = true;
    }

    // Sent/error flash timer
    if (sentFlash || errorFlash) {
        if (now - flashStartMs >= config::ui::kSentFlashMs) {
            sentFlash = false;
            errorFlash = false;
            dirty = true;
        }
    }

    // Read button states once (consumed on first call)
    const bool singleClick = M5.BtnA.wasSingleClicked();
    const bool doubleClick = M5.BtnA.wasDoubleClicked();
    const bool longPress   = M5.BtnA.pressedFor(config::kButtonHoldMs);

    // Physical button activity restores brightness and resets timer
    if (singleClick || doubleClick) {
        lastActionMs = now;
        if (displayDimmed) {
            displayDimmed = false;
            hal::display::brighten();
        }
    }

    // Gather input events for state machine (BOTH touch AND physical buttons)
    app::InputEvents events;
    events.holdComplete    = (touchEvent.gesture == hal::TouchGesture::HoldComplete) && !showHistory;
    events.singleClick     = singleClick;
    events.doubleClick     = doubleClick;
    events.longPress       = longPress;
    events.touchActive     = touchEvent.touching;
    events.isCharging      = charging;
    events.chargingChanged = chargingChanged;
    events.rxReady         = pendingRx;
    events.timeSinceLastActionMs = now - lastActionMs;

    // State machine
    appState = app::nextState(appState, events);

    switch (appState) {
        case app::State::Transmitting: {
            lastActionMs = now;
            const bool success = handleTransmit(now);
            if (success) {
                sentFlash = true;
            } else {
                errorFlash = true;
            }
            flashStartMs = now;
            dirty = true;
            appState = app::State::Idle;
            break;
        }

        case app::State::Receiving:
            handleReceive(now);
            appState = app::State::Idle;
            break;

        case app::State::EnteringSleep:
            handleSleep();
            appState = app::State::Idle;
            break;

        case app::State::PoweringOff:
            handlePowerOff();
            break;

        case app::State::Idle:
            // Physical button: double click advances message
            if (events.doubleClick) {
                lastActionMs = now;
                cannedMessages.next();
                showHint = false;
                dirty = true;
            }
            // Physical button: single click resets action timer
            if (events.singleClick) {
                lastActionMs = now;
                showHint = false;
            }
            break;
    }

    // Render if dirty and display is awake
    if (dirty && !displaySleeping) {
        renderFrame(now);
        dirty = false;
    }

    // Display dimming: reduce brightness after inactivity
    const uint32_t inactiveMs = now - lastActionMs;
    if (!displaySleeping && !displayDimmed
        && inactiveMs >= config::kDimTimeoutMs && inactiveMs < config::kSleepTimeoutMs) {
        displayDimmed = true;
        hal::display::dim();
    }

    // Idle delay: let CPU enter light sleep between loop iterations
    if (!dirty) {
        delay(config::kLoopIdleDelayMs);
    }
}
