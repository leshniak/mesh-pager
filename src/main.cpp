/// Main application glue — Arduino setup() and loop().
///
/// This file ties together all the modules (radio, display, touch, buzzer,
/// power management, state machine, canned messages, UI renderer) into the
/// main event loop. It follows a "dirty flag" rendering pattern:
///
///   1. Collect input events (touch, button, radio, charging, timers)
///   2. Feed events into the pure state machine (AppState)
///   3. Execute side effects based on state transitions (TX, RX, sleep, power-off)
///   4. Set `dirty = true` whenever the UI needs to update
///   5. If dirty && display awake, call renderer.render() (full-frame redraw)
///
/// All state is module-level (anonymous namespace) rather than global. The
/// secrets.h header provides `channelName`, `channelKey`, and `messages[]`
/// — these are compile-time constants defined per-device.

#include <M5Unified.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include "config/AppConfig.h"
#include "config/Pins.h"
#include "config/UIConfig.h"
#include "protocol/MeshTypes.h"
#include "protocol/MeshCodec.h"
#include "protocol/MeshPacket.h"
#include "protocol/PacketDedup.h"
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

// ── Module instances ────────────────────────────────────────────────────────
hal::RadioHal radioHal;           ///< SX1262 radio driver (SPI, interrupt-driven RX)
hal::TouchInput touchInput;       ///< FT6336 touch gesture recognizer
app::CannedMessages cannedMessages; ///< Canned message selector with NVS persistence
ui::Renderer renderer;             ///< Full-frame LGFX sprite renderer
ui::ToastManager toastManager;     ///< Toast notification + message history

// ── Mesh identity ───────────────────────────────────────────────────────────
// Derived at startup from secrets.h (channel key/name) and the ESP32 MAC address.
uint8_t channelKeyBytes[protocol::kKeyLen] = {};  ///< AES-256 key (decoded from base64)
uint8_t channelHashByte = 0;    ///< XOR-based channel fingerprint (1 byte)
protocol::NodeId nodeId = 0;    ///< Our node ID (last 4 bytes of MAC, big-endian)
uint32_t packetIdCounter = 0;   ///< Monotonically increasing packet ID (random seed)

// ── Application state ───────────────────────────────────────────────────────
app::State appState = app::State::Idle;  ///< Current state machine state
uint32_t lastActionMs = 0;    ///< millis() of last user interaction (for sleep timer)
bool wasCharging = false;      ///< Previous loop's charging state (for edge detection)
bool pendingRx = false;        ///< Set by radio ISR callback when a packet arrives
bool stayAwake = false;        ///< True when stay-awake lock is active (double-click toggle)
uint32_t stayAwakeStartMs = 0; ///< millis() when stay-awake was enabled (for safety timeout)
uint8_t rxFrameBuf[protocol::kRxBufferLen] = {};  ///< Buffer for received radio frame
size_t rxFrameLen = 0;         ///< Length of the received frame in rxFrameBuf
protocol::PacketDedup<> packetDedup;  ///< Deduplication cache (64 entries, 10min expiry)

// ── Cached sensor readings ──────────────────────────────────────────────────
// Read once per loop iteration (at the top, before rendering) to avoid I2C
// bus contention if the radio ISR fires mid-transaction.
uint8_t cachedBatteryPercent = 0;  ///< Battery level 0–100, updated each loop
bool cachedCharging = false;       ///< USB charging state, updated each loop

// ── UI state flags ──────────────────────────────────────────────────────────
// These are the "dirty flag" rendering system: any change sets dirty=true,
// and the end of loop() redraws if needed.
bool dirty = true;             ///< True when the screen needs to be redrawn
bool displaySleeping = false;  ///< True when the display is powered off (deep sleep pending)
bool showHistory = false;      ///< True when the history overlay is visible
bool showHint = true;          ///< True until the user's first interaction (shows "swipe | hold")

// ── Touch hold state ────────────────────────────────────────────────────────
bool isHolding = false;        ///< True while the user is holding finger down for send
float holdProgress = 0.0f;     ///< 0.0–1.0 progress of the hold-to-send gesture

// ── Sent/error flash ────────────────────────────────────────────────────────
bool sentFlash = false;        ///< True during the brief teal background flash after TX
bool errorFlash = false;       ///< True during the brief red background flash on TX error
uint32_t flashStartMs = 0;    ///< millis() when the flash started (auto-clears after kSentFlashMs)

/// Radio RX callback — called from RadioHal when a packet arrives.
/// Copies the raw frame into rxFrameBuf and sets the pendingRx flag.
/// The actual parsing happens in handleReceive() during the main loop,
/// not in the callback (keeps ISR-context work minimal).
///
/// rxProcessing guards against a race: if we're mid-parse in handleReceive(),
/// a new packet arriving would overwrite the buffer with partial data.
/// In that case we drop the new packet — it will likely be received again
/// via another relay path (and deduplication handles it if not).
volatile bool rxProcessing = false;  ///< True while handleReceive() is reading rxFrameBuf

void onRadioRx(const uint8_t* data, size_t len) {
    if (rxProcessing) return;  // Buffer in use — drop to avoid corruption
    if (len <= protocol::kRxBufferLen) {
        memcpy(rxFrameBuf, data, len);
        rxFrameLen = len;
        pendingRx = true;
    }
}

/// Transmit the current canned message as a Meshtastic broadcast.
///
/// Steps:
///   1. Encode message text as a protobuf Data payload (portNum=1, TEXT_MESSAGE)
///   2. Build the 16-byte mesh header (dest=broadcast, source=nodeId, etc.)
///   3. Encrypt the payload in-place with AES-256-CTR
///   4. Send the frame via the SX1262 radio (blocking TX)
///   5. On success: play TX tone, add to toast history
///
/// Returns true on successful transmission, false on any error.
bool handleTransmit(uint32_t now) {
    auto text = cannedMessages.current();
    if (text.empty()) return false;

    // Step 1: Encode text into protobuf Data message
    uint8_t payload[4 + protocol::kMaxTextLen];
    const size_t payloadLen = protocol::encodeTextPayload(
        reinterpret_cast<const uint8_t*>(text.data()), text.size(),
        payload, sizeof(payload));
    if (payloadLen == 0) {
        hal::display::logError("TX encode failed");
        return false;
    }

    // Step 2: Build mesh header
    protocol::PacketHeader hdr;
    hdr.dest = protocol::kBroadcastAddr;
    hdr.source = nodeId;
    hdr.packetId = packetIdCounter++;
    hdr.flags = protocol::makeMeshFlags(
        config::kHopLimit, config::kWantAck, config::kViaMqtt, config::kHopStart);
    hdr.channelHash = channelHashByte;

    // Step 3 & 4: Build frame (header + encrypted payload) and transmit
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

/// Process a pending received radio frame.
///
/// Steps:
///   1. Parse the mesh header and decrypt the payload
///   2. Filter: wrong channel hash, own packets (echo), empty text → discard
///   3. On valid text message: play RX tone, add to toast + history, set dirty
///
/// Note: parsePacket() sanitizes non-printable characters (replaces with '.'),
/// so the output text is safe to display directly.
void handleReceive(uint32_t now) {
    if (!pendingRx) return;
    pendingRx = false;
    rxProcessing = true;  // Lock buffer — ISR will drop packets while we parse

    protocol::PacketHeader hdr;
    char textOut[200];
    size_t textLen = 0;

    auto err = protocol::parsePacket(
        rxFrameBuf, rxFrameLen, channelKeyBytes,
        hdr, textOut, sizeof(textOut), textLen);

    rxProcessing = false;  // Unlock buffer — safe for ISR to write again

    if (err != protocol::MeshError::Ok) return;  // Decryption/parse failure
    if (hdr.channelHash != channelHashByte) return;  // Different channel
    if (hdr.source == nodeId) return;  // Our own echo (relayed back)
    if (textLen == 0) return;  // Non-text packet (e.g., position, telemetry)
    if (packetDedup.isDuplicate(hdr.source, hdr.packetId, now)) return;  // Already seen via another relay

    lastActionMs = now;  // Incoming messages keep the device awake

    // Wake display from stay-awake standby on valid message
    if (displaySleeping && stayAwake) {
        displaySleeping = false;
        hal::display::wakeup();
    }

    const int8_t snr = static_cast<int8_t>(radioHal.lastSnr());
    const uint8_t hopStart = (hdr.flags >> 5) & 0x07;
    const uint8_t hopLimit = hdr.flags & 0x07;
    const uint8_t hops = (hopStart >= hopLimit) ? (hopStart - hopLimit) : 0;
    hal::display::logInfo(">> <!%08X> snr=%d hops=%u %s", hdr.source, snr, hops, textOut);
    hal::playRxTone();
    toastManager.addMessage(hdr.source, textOut, now, snr, hops);
    dirty = true;
}

/// Enter sleep mode. Three paths:
///   - **If charging**: display off, device stays alive (wake on touch or charge change).
///   - **If stay-awake lock**: display off, radio stays active (wake on RX or button).
///   - **Otherwise**: full deep sleep (radio off, wake on KEY1 button press only).
void handleSleep() {
    if (displaySleeping) return;  // Already asleep — avoid re-entering on every loop

    displaySleeping = true;
    touchInput.consumeNextTouch();  // Next touch = wake, not action

    if (cachedCharging || stayAwake) {
        hal::display::sleep();    // Display off only — MCU and radio stay alive
        return;
    }

    // Full deep sleep path — play tone while hardware is still active
    cannedMessages.save();        // Persist selected message index to NVS
    radioHal.sleep();             // Power down SX1262
    hal::power::ledOff();         // Turn off LED
    hal::playSleepTone();         // Audio feedback: two beeps
    hal::display::sleep();        // Display off last (after tone)
    hal::power::enterDeepSleep(); // Never returns — reboots on wake
}

/// Full power-off via PMIC. Saves state, powers down radio, plays descending
/// tone, then tells the AXP2101 to cut all power. Only USB reconnect can restart.
void handlePowerOff() {
    cannedMessages.save();
    radioHal.sleep();
    hal::playPowerOffTone();
    hal::power::powerOff();  // Never returns
}

/// Populate a RenderState snapshot from the current module-level state and
/// call the renderer to draw one frame. Called only when dirty==true.
void renderFrame(uint32_t now) {
    ui::RenderState state;
    state.channelName = channelName;
    state.nodeId = nodeId;
    state.batteryPercent = cachedBatteryPercent;
    state.isCharging = cachedCharging;
    state.stayAwake = stayAwake;
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

/// Arduino setup() — called once on boot (cold start or deep-sleep wake).
///
/// Initialization order matters:
///   1. M5Unified (display driver, I2C, SPI, PMIC)
///   2. Power management (IO expanders, charging config)
///   3. Display (orientation, brightness)
///   4. Touch input (FT6336 gesture threshold)
///   5. Renderer (allocate off-screen sprite)
///   6. Canned messages (load from secrets.h + restore NVS index)
///   7. Node identity (derive from WiFi MAC, then turn WiFi off)
///   8. Channel key (decode base64 → AES-256 key bytes)
///   9. Radio (SX1262 init, start continuous RX)
///   10. Optional: send-on-wake (auto-transmit if waking from deep sleep)
void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = config::kSerialBaudRate;
    M5.begin(cfg);

    // Disable unused peripherals for power savings
    M5.Imu.sleep();                           // BMI270 not used (suspended via power manager too)
    M5.Power.setExtOutput(false);             // Disable external 5V output rail
    M5.Speaker.setVolume(config::kSpeakerVolume);  // Max volume for small piezo
    setCpuFrequencyMhz(config::kCpuFreqMHz); // 80MHz saves ~25% power vs 160MHz default

    hal::power::init();    // I2C, IO expanders, PMIC charging parameters
    hal::display::init();  // Portrait orientation, full brightness, clear screen
    touchInput.init();     // FT6336 flick threshold
    renderer.init();       // Allocate 135×240 RGB565 sprite in internal SRAM

    // Load canned messages from compile-time array (secrets.h) and restore
    // the persisted selection index from NVS flash
    constexpr size_t msgCount = sizeof(messages) / sizeof(messages[0]);
    cannedMessages.init(messages, msgCount);

    // Derive node ID from the ESP32 WiFi MAC address (last 4 bytes).
    // WiFi is only enabled briefly to read the MAC, then immediately disabled.
    uint8_t mac[6] = {};
    WiFi.mode(WIFI_STA);
    WiFi.macAddress(mac);
    WiFi.mode(WIFI_OFF);

    nodeId = protocol::nodeIdFromMac(mac);

    // Seed the packet ID counter with hardware RNG to avoid collisions after
    // reboots. The offset range (0x1000–0xFFFF) avoids very low IDs that
    // could be confused with Meshtastic protocol constants.
    randomSeed(static_cast<uint32_t>(esp_random()));
    packetIdCounter = static_cast<uint32_t>(random(0x1000, 0xFFFF));

    // Decode the base64-encoded AES-256 channel key from secrets.h
    if (!protocol::decodeBase64Key(channelKey, channelKeyBytes)) {
        hal::display::logError("Channel key decode failed");
        while (true) delay(1000);  // Fatal error — halt
    }
    // Compute the 1-byte channel hash (XOR fingerprint of name + key)
    channelHashByte = protocol::computeChannelHash(channelName, channelKeyBytes);

    // Initialize the SX1262 radio and start continuous RX
    radioHal.setRxCallback(onRadioRx);
    if (radioHal.begin() != protocol::MeshError::Ok) {
        hal::display::logError("Radio init failed");
        while (true) delay(1000);  // Fatal error — halt
    }

    hal::display::logInfo("NodeId: !%08X", nodeId);
    hal::display::logInfo("Channel: %s", channelName);
    hal::display::logInfo("Battery: %d%%", M5.Power.getBatteryLevel());

    lastActionMs = millis();

    // Send-on-wake feature: if enabled and this is a deep-sleep wake (not cold
    // boot), automatically transmit the current canned message. Useful for
    // gate/garage remote use cases where button press = wake + send + sleep.
    // Guarded with `if constexpr` — when kSendOnWake is false, this entire
    // block is eliminated by the compiler (zero overhead).
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

/// Arduino loop() — main event loop, runs continuously.
///
/// Structure:
///   1. Poll hardware (M5.update for button/touch, radioHal.pollRx for radio)
///   2. Handle edge-triggered events (charge state change, touch wake)
///   3. Process touch gestures → update UI state
///   4. Update toast manager (countdown timer)
///   5. Clear expired sent/error flash
///   6. Read physical button states
///   7. Feed all events into the pure state machine → execute side effects
///   8. Render if dirty
///   9. Handle display dimming
///   10. Idle delay (lets ESP32 enter automatic light sleep between iterations)
void loop() {
    M5.update();       // Read button, touch, and power states from hardware
    radioHal.pollRx(); // Check for pending radio packets (interrupt-driven)

    const uint32_t now = millis();

    // Cache I2C sensor reads once per loop — keeps them out of render path
    // and avoids bus contention with radio/touch interrupts.
    const int32_t rawBat = M5.Power.getBatteryLevel();
    cachedBatteryPercent = (rawBat >= 0 && rawBat <= 100) ? static_cast<uint8_t>(rawBat) : 0;
    cachedCharging = hal::power::isCharging();

    const bool charging = cachedCharging;

    // ── 1. Detect USB charge state changes ──────────────────────────────────
    // Plugging/unplugging USB should wake the display and reset the sleep timer.
    const bool chargingChanged = (wasCharging != charging);
    if (chargingChanged) {
        wasCharging = charging;
        lastActionMs = now;
        if (displaySleeping) {
            displaySleeping = false;
            hal::display::wakeup();
            touchInput.consumeNextTouch();  // Don't treat charge-wake touch as gesture
        }
        dirty = true;
    }

    // ── 2. Stay-awake standby ──────────────────────────────────────────────
    // In standby the sleep timeout is permanently exceeded, so the state machine
    // would keep returning EnteringSleep and never reach Receiving. Process RX
    // and button wake directly here, before the state machine runs.
    if (displaySleeping && stayAwake) {
        if (pendingRx) {
            handleReceive(now);  // Wakes display only on valid channel messages
        }
        if (M5.BtnA.wasClicked()) {
            displaySleeping = false;
            lastActionMs = now;
            hal::display::wakeup();
            dirty = true;
            // Consume any pending single/double-click so the wake press
            // doesn't also trigger a send or toggle stay-awake.
            M5.BtnA.wasSingleClicked();
            M5.BtnA.wasDoubleClicked();
            return;  // Skip the rest of this loop iteration
        }
    }

    // ── 3. Process touch input ──────────────────────────────────────────────
    auto touchEvent = touchInput.update();

    if (touchEvent.touching) {
        lastActionMs = now;  // Any touch resets the inactivity timer

        // Handle display wake-up from touch (while charging, display sleeps but
        // device stays alive — touch can wake it without full deep-sleep cycle)
        if (displaySleeping && touchEvent.gesture == hal::TouchGesture::Wake) {
            displaySleeping = false;
            hal::display::wakeup();
            dirty = true;
        }
    }

    // ── 4. Handle touch gestures (only when display is awake) ───────────────
    if (!displaySleeping) {
        switch (touchEvent.gesture) {
            case hal::TouchGesture::SwipeLeft:
                cannedMessages.previous();     // Navigate to previous message
                showHint = false;
                isHolding = false;
                holdProgress = 0.0f;
                dirty = true;
                break;

            case hal::TouchGesture::SwipeRight:
                cannedMessages.next();         // Navigate to next message
                showHint = false;
                isHolding = false;
                holdProgress = 0.0f;
                dirty = true;
                break;

            case hal::TouchGesture::HoldTick:
                if (!showHistory) {            // Don't show hold progress over history overlay
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
                    dirty = true;              // State machine will handle TX
                }
                break;

            case hal::TouchGesture::SwipeDown:
                if (!showHistory) {
                    showHistory = true;        // Open message history overlay
                    dirty = true;
                }
                break;

            case hal::TouchGesture::SwipeUp:
                if (showHistory) {
                    showHistory = false;       // Close message history overlay
                    dirty = true;
                }
                break;

            case hal::TouchGesture::None:
                // Finger lifted while holding — cancel the hold (no transmit)
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

    // ── 5. Toast countdown timer ────────────────────────────────────────────
    if (toastManager.update(now)) {
        dirty = true;  // Redraw to animate the countdown bar
    }

    // ── 6. Sent/error flash auto-clear ──────────────────────────────────────
    if (sentFlash || errorFlash) {
        if (now - flashStartMs >= config::ui::kSentFlashMs) {
            sentFlash = false;
            errorFlash = false;
            dirty = true;
        }
    }

    // ── 7. Physical button (KEY1) states ────────────────────────────────────
    // M5.BtnA.was*() methods are consumed on first call — read once and reuse.
    // Ignore all button events during the post-wake debounce window to prevent
    // the wake button press from triggering power-off or send.
    const bool inDebounce = (now < config::kDebounceGuardMs);
    const bool singleClick = M5.BtnA.wasSingleClicked() && !inDebounce;
    const bool doubleClick = M5.BtnA.wasDoubleClicked() && !inDebounce;
    const bool longPress   = M5.BtnA.pressedFor(config::kButtonHoldMs) && !inDebounce;

    if (singleClick || doubleClick) {
        lastActionMs = now;
    }

    // ── 8. Stay-awake safety timeout ─────────────────────────────────────────
    // After kStayAwakeMaxMs (10 min), force deep sleep with audio warning.
    if (stayAwake && (now - stayAwakeStartMs) >= config::kStayAwakeMaxMs) {
        stayAwake = false;
        cannedMessages.save();
        radioHal.sleep();
        hal::power::ledOff();
        hal::playSleepTone();
        hal::display::sleep();
        hal::power::enterDeepSleep();  // Never returns
    }

    // ── 9. State machine ────────────────────────────────────────────────────
    // Collect all input events into a struct and feed to the pure state machine.
    // The state machine only returns the next state — all side effects are here.
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

    appState = app::nextState(appState, events);

    switch (appState) {
        case app::State::Transmitting: {
            lastActionMs = now;
            const bool success = handleTransmit(now);
            sentFlash = success;
            errorFlash = !success;
            flashStartMs = now;
            dirty = true;
            appState = app::State::Idle;  // TX is blocking — return to Idle immediately
            break;
        }

        case app::State::Receiving:
            handleReceive(now);
            appState = app::State::Idle;
            break;

        case app::State::EnteringSleep:
            handleSleep();                 // May not return (deep sleep)
            appState = app::State::Idle;   // Returns here only if charging (display-only sleep)
            break;

        case app::State::PoweringOff:
            handlePowerOff();              // Never returns
            break;

        case app::State::Idle:
            // Physical button double-click: toggle stay-awake lock
            if (events.doubleClick) {
                lastActionMs = now;
                stayAwake = !stayAwake;
                if (stayAwake) stayAwakeStartMs = now;
                dirty = true;
            }
            // Physical button single-click: just reset the inactivity timer
            // (TX is handled by the state machine via events.singleClick)
            if (events.singleClick) {
                lastActionMs = now;
                showHint = false;
            }
            break;
    }

    // ── 10. Render ──────────────────────────────────────────────────────────
    if (dirty && !displaySleeping) {
        renderFrame(now);
        dirty = false;
    }

    // ── 11. Idle delay ──────────────────────────────────────────────────────
    // When nothing needs immediate attention, delay() lets the ESP32 enter
    // automatic light sleep (modem sleep at 80MHz), saving power between
    // loop iterations.
    if (!dirty) {
        delay(config::kLoopIdleDelayMs);
    }
}
