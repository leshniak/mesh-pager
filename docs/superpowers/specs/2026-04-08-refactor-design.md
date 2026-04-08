# Mesh Remote — Professional Refactor Design

**Date:** 2026-04-08
**Goal:** Refactor the mesh-remote codebase for maintainability, robustness, and testability while preserving the exact current feature set. Prepare clean UI abstraction for future touch screen interface.
**C++ style:** Modern embedded-safe C++ (constexpr, enum class, std::array, std::string_view, namespaces). No heap allocation, no exceptions, no RTTI, no std::string.

## Module Structure

```
include/
  config/
    Pins.h              — GPIO pin definitions (constexpr, header-only)
    RadioConfig.h       — LoRa parameters: freq, SF, BW, coding rate (header-only)
    AppConfig.h         — Timeouts, buzzer frequencies, charge settings (header-only)
  protocol/
    MeshTypes.h         — NodeId, PacketId, PacketHeader struct, PortNum enum
    MeshPacket.h        — buildPacket(), parsePacket()
    MeshCrypto.h        — AES-256-CTR encrypt/decrypt, nonce construction
    MeshCodec.h         — Protobuf encode/decode, base64 key decode, channel hash
  hal/
    RadioHal.h          — SX1262 wrapper: begin, tx, rx, sleep
    PowerManager.h      — Charging, deep sleep entry/exit, peripheral power-down
    Buzzer.h            — Tone pattern functions
    Display.h           — Thin display abstraction (future touch screen seam)
  app/
    AppState.h          — Device state machine
    CannedMessages.h    — Message storage, index persistence, navigation
  secrets.h             — Channel name, key, messages array (unchanged)

src/
  protocol/MeshPacket.cpp, MeshCrypto.cpp, MeshCodec.cpp
  hal/RadioHal.cpp, PowerManager.cpp, Buzzer.cpp, Display.cpp
  app/AppState.cpp, CannedMessages.cpp
  main.cpp              — setup() + loop() orchestrator (~50 lines)
```

### Principles

- One responsibility per file.
- `config/` is header-only constexpr values.
- `protocol/` has zero hardware dependencies — pure logic.
- `hal/` owns all hardware interaction.
- `app/` contains application logic that uses hal/ and protocol/.
- `main.cpp` is a thin orchestrator.

## State Machine

```cpp
enum class AppState : uint8_t {
    Idle,           // Waiting for input, display shows current message
    Transmitting,   // Sending packet, blocking until TX complete
    Receiving,      // Incoming packet detected, processing
    EnteringSleep,  // Powering down peripherals
    Sleeping,       // Deep sleep (wake on button)
    PoweringOff     // User-requested shutdown
};
```

### Transitions

- `Idle` + single-click → `Transmitting` → `Idle`
- `Idle` + double-click → `Idle` (advance message index, update display)
- `Idle` + hold 1s → `PoweringOff`
- `Idle` + 15s timeout → `EnteringSleep` → `Sleeping`
- `Idle` + rx packet → `Receiving` → `Idle`
- Charging detected → inhibit sleep transition

## Protocol Layer (namespace mesh::protocol)

Zero hardware dependencies. All functions take buffers in/out.

### MeshTypes.h

- `using NodeId = uint32_t;`
- `using PacketId = uint32_t;`
- `PacketHeader` struct (16 bytes): dest, source, packetId, flags, channelHash, nextHop/relayNode
- `enum class PortNum : uint8_t { TextMessageApp = 1 };`

### MeshCrypto

- `aesCtrEncrypt(key, nonce, plaintext, len, out)` → error code
- `aesCtrDecrypt(key, nonce, ciphertext, len, out)` → error code
- `buildNonce(packetId, nodeId)` → nonce array

### MeshCodec

- `encodeTextPayload(text, out, outLen)` → error code
- `decodeTextPayload(data, len, outText, outLen)` → error code
- `decodeBase64Key(b64, outKey)` → bool
- `computeChannelHash(channelName, key)` → uint8_t

### MeshPacket

- `buildPacket(header, plaintext, len, key, outBuf, outLen)` → error code
  - Calls MeshCodec::encodeTextPayload, MeshCrypto::aesCtrEncrypt, assembles header
- `parsePacket(buf, len, key, outHeader, outText, outTextLen)` → error code
  - Parses header, calls MeshCrypto::aesCtrDecrypt, MeshCodec::decodeTextPayload

## HAL Layer (namespace mesh::hal)

### RadioHal

- Owns `SX1262` instance.
- `begin(config)` → RadioError
- `transmit(buf, len)` → RadioError
- `startReceive()` → RadioError
- `readPacket(buf, maxLen, outLen)` → RadioError
- `sleep()` → void
- Never calls Serial or M5.Log.

### PowerManager

- `configureCharging(current, voltage)` → void
- `isCharging()` → bool
- `setupDeepSleep()` → void
- `enterDeepSleep()` → void
- `exitDeepSleep()` → void
- Absorbs all I2C peripheral power-down logic from current DeepSleep.cpp.

### Buzzer

- `playTxTone()`, `playRxTone()`, `playSleepTone()`, `playPowerOffTone()`
- Frequencies/durations defined in AppConfig.h.

### Display

- `init()`, `showMessage(std::string_view)`, `showStatus(std::string_view)`, `clear()`
- Current implementation calls M5.Display + M5.Log.
- Future touch UI replaces this file only.

## App Layer (namespace mesh::app)

### AppState

- Holds current state enum.
- `update(inputs)` → returns new state based on button events, radio events, timers.
- Pure logic — receives inputs, returns state transitions. Does not call hardware directly.

### CannedMessages

- `init()` — loads persisted index from Preferences.
- `current()` → `std::string_view`
- `next()` — advances index, wraps, persists.
- `count()` → `size_t`

## Error Handling

```cpp
enum class RadioError : uint8_t {
    Ok,
    InitFailed,
    TxFailed,
    RxFailed,
    InvalidPacket,
    DecryptionFailed
};
```

- All HAL and protocol functions return typed error codes.
- main.cpp handles errors with display messages and buzzer feedback.
- No exceptions, no heap allocation.

## Modern C++ Patterns

| Pattern | Usage |
|---------|-------|
| `constexpr` | All config values, pin definitions |
| `enum class` | States, errors, port numbers |
| `std::array` | Fixed buffers replacing raw C arrays |
| `std::string_view` | Text references without copies |
| `#pragma once` | All headers |
| Namespaces | `mesh::protocol`, `mesh::hal`, `mesh::app` |

## main.cpp Orchestration

```
setup():
  M5.begin()
  Display::init()
  CannedMessages::init()
  PowerManager::configureCharging()
  RadioHal::begin()
  Display::showMessage(CannedMessages::current())

loop():
  M5.update()
  Run state machine:
    Idle:
      check buttons → dispatch transitions
      check rx → Receiving
      check timeout → EnteringSleep
    Transmitting:
      build + send packet via MeshPacket + RadioHal
      play tx tone
      → Idle
    Receiving:
      read + parse packet via RadioHal + MeshPacket
      display received text, play rx tone
      → Idle
    EnteringSleep:
      play sleep tone
      RadioHal::sleep()
      PowerManager::enterDeepSleep()
    PoweringOff:
      play power off tone
      M5.Power.powerOff()
```

## What Does NOT Change

- `secrets.h` format and content
- Meshtastic protocol compatibility (same packet format, same encryption)
- Radio parameters (869.525 MHz, SF9, BW250, CR5)
- User interaction (single-click send, double-click cycle, hold power off)
- Sleep behavior and timeouts
- Buzzer tone sequences
