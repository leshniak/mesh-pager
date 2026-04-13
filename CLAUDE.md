# mesh-pager — Claude Code Guide

## Build & Upload

```bash
pio run                                            # build
pio run -t upload --upload-port <PORT>              # flash
pio test -e native                                 # run host-side unit tests
```

**Upload port** — ask the user for the port if not already specified in the conversation. Never auto-detect.

## Hardware Quirks

- **ESP32-C6 USB-JTAG**: the serial port disappears when the device enters deep sleep (15s inactivity). To re-flash: press the physical button to wake, then immediately upload. If that fails, hold BOOT + press RESET to enter download mode.
- **Serial monitor doesn't work reliably** over USB-JTAG on this board. Use on-screen debug (toast messages) instead of serial logging when debugging.
- **Deep sleep kills USB** — expect upload failures. Just retry after waking the device. This is normal, not a bug.

## Architecture

The project follows a layered structure with clear separation:

```
config/   — compile-time constants (pins, radio params, UI layout, colors)
protocol/ — Meshtastic-compatible packet encoding, AES-256-CTR encryption, protobuf codec
hal/      — hardware abstraction: RadioHal (SX1262 via RadioLib), TouchInput, Display, Buzzer, PowerManager
app/      — application logic: state machine (AppState), CannedMessages (NVS persistence)
ui/       — rendering: Renderer (LGFX_Sprite double-buffer), ToastManager (history ring buffer)
main.cpp  — glue: setup, loop, event routing, state transitions
```

## Key Conventions

- **All UI constants in `UIConfig.h`** — colors (RGB565), dimensions, timing. No magic numbers in rendering code.
- **All app constants in `AppConfig.h`** — timeouts, tone frequencies, mesh flags.
- **Dirty-flag rendering** — only redraw when `dirty = true`. Set it whenever visible state changes.
- **No LGFX clipping** — the display library has no clip rect support. Use plain rects, not rounded rects (rounded corners were tried and abandoned).
- **RGB565 color format** — use the `rgb565(r, g, b)` constexpr helper in UIConfig.h.
- **Namespace structure**: `mesh::config`, `mesh::protocol`, `mesh::hal`, `mesh::app`, `mesh::ui`.
- **State machine** in `AppState.cpp` is pure (no side effects) — it takes `InputEvents` and returns the next `State`. Side effects happen in `main.cpp` switch cases.
- **Stay-awake lock** — double-click toggles standby mode. Screen turns off after normal timeout but radio stays active. Only valid text messages on our channel (same filtering as toast) or BtnA wake the screen. Emergency deep sleep with tone after `kStayAwakeMaxMs` (10 min). Red dot in status bar near battery.
- **RX keeps device awake** — incoming messages reset the sleep timer.
- **Packet deduplication** — `PacketDedup` (64 entries, 10 min expiry) filters duplicate packets received via multiple relay paths.
- **No display dimming** — Nesso N1 backlight is IO-expander GPIO (on/off only, not PWM). `setBrightness()` has no effect.

## Display

- 135x240 pixels, portrait orientation, 16-bit color
- Single `LGFX_Sprite` double-buffered to internal SRAM (no PSRAM on ESP32-C6)
- Layout top-to-bottom: status bar (18px) → toast (if active) → message card (fills rest)
- Status bar: node ID (left), stay-awake dot (if active), battery icon + percentage (right)
- Message card: hint text → channel name → centered message → page dots → hold progress bar

## Touch Gestures

Handled by `TouchInput` which produces `TouchEvent` with a `TouchGesture` enum:
- `SwipeLeft` / `SwipeRight` — navigate messages
- `SwipeDown` / `SwipeUp` — toggle history overlay
- `HoldTick` / `HoldComplete` — hold-to-send with progress (1000ms)
- `Wake` — first touch after display sleep

Swipe detection compares `absDx` vs `absDy` against `kSwipeThresholdPx` (20px).

## Meshtastic Protocol

Custom implementation (not the official Meshtastic library):
- **Packet format**: 16-byte mesh header + AES-256-CTR encrypted protobuf payload
- **Encryption**: mbedtls AES-256-CTR, nonce = packetId(8) + sourceNode(4) + pad(4)
- **Channel hash**: XOR of channel name bytes XOR key bytes
- **Broadcast only**: dest = `0xFFFFFFFF`. ACKs don't work for broadcast (Meshtastic limitation).
- **PortNum 1** = TextMessage. Other port numbers are received but silently ignored.

## Keeping in Sync with Meshtastic

This project implements a **minimal subset** of the Meshtastic over-the-air protocol — just enough to send/receive text messages on a shared channel. It is NOT built on the official Meshtastic firmware or libraries. This means protocol changes in upstream Meshtastic can silently break compatibility.

### What we implement

| Layer | Our code | Upstream reference |
|-------|----------|-------------------|
| Packet framing | `MeshPacket.cpp` — 16-byte header, field offsets in `MeshTypes.h` (`kOff*` constants) | [mesh.proto → MeshPacket](https://github.com/meshtastic/protobufs/blob/master/meshtastic/mesh.proto) |
| Encryption | `MeshCrypto.cpp` — AES-256-CTR, nonce layout in `MeshTypes.h` (`kNonce*` constants) | [crypto.md](https://meshtastic.org/docs/overview/encryption/) |
| Protobuf codec | `MeshCodec.cpp` — hand-rolled encode/decode for `Data` message (portnum + payload only) | [portnums.proto](https://github.com/meshtastic/protobufs/blob/master/meshtastic/portnums.proto) |
| Channel hash | `MeshCodec.cpp` — `computeChannelHash()` XOR algorithm | [channel.proto](https://github.com/meshtastic/protobufs/blob/master/meshtastic/channel.proto) |
| Radio params | `RadioConfig.h` — frequency, SF, BW, sync word matching MediumFast EU preset | [radiomaster.h / modem presets](https://github.com/meshtastic/firmware/blob/master/src/mesh/RadioInterface.cpp) |
| Node ID | `MeshCodec.cpp` — `nodeIdFromMac()` last-4-bytes-of-MAC convention | [NodeDB.cpp](https://github.com/meshtastic/firmware/blob/master/src/mesh/NodeDB.cpp) |

### What we DON'T implement

- Routing / acknowledgment / retransmission (we broadcast-only)
- Admin channel, remote config, position, telemetry, or any PortNum other than TextMessage
- NodeDB, neighbor discovery, or any mesh intelligence
- Protobuf schema evolution (we decode only fields 1 and 2 of the Data message, skip the rest)

### How to check for breaking changes

If Meshtastic devices stop seeing our messages (or vice versa), check these in order:

1. **Packet header layout** — has the 16-byte header structure changed? Compare `MeshTypes.h` offsets against [mesh.proto MeshPacket](https://github.com/meshtastic/protobufs/blob/master/meshtastic/mesh.proto). Focus on: field order, byte widths, the `flags` bitfield packing (hopLimit/wantAck/viaMqtt/hopStart).

2. **Encryption nonce format** — has the AES-CTR nonce construction changed? Our nonce is `packetId(8 LE) + sourceNode(4 LE) + zeros(4)`. Check [Meshtastic encryption docs](https://meshtastic.org/docs/overview/encryption/).

3. **Channel hash algorithm** — still XOR of name bytes XOR key bytes? If this changes, our packets get filtered out before decryption is even attempted.

4. **Radio presets** — has LongFast changed its SF/BW/frequency? Compare `RadioConfig.h` against the modem preset definitions in Meshtastic firmware. Even a 1-bit difference in sync word (`0x2B`) means total silence.

5. **Protobuf Data message** — have fields 1 (portnum) or 2 (payload) changed wire type or field number? Our codec skips unknown fields for forward-compatibility, so new fields won't break us, but changes to existing fields will.

### Testing interop

The simplest test: send a canned message from this device and verify it appears on a stock Meshtastic device (phone app or another node) on the same channel. Then send from Meshtastic and verify our device shows the toast. If either direction fails, walk through the checklist above.

## Secrets

`include/secrets.h` contains channel name, base64 key, and canned message array. Never commit it. See `secrets.example.h` for the template.

## Versioning

Uses [Semantic Versioning](https://semver.org/) (`vMAJOR.MINOR.PATCH`). Tag releases on the master branch.

## Documentation

For any non-cosmetic change (features, bug fixes, protocol updates, config changes), review and update:
- **CLAUDE.md** — architecture, conventions, protocol details, pitfalls
- **README.md** — user-facing feature list, controls, screenshots
- **SVG mockups** (`docs/images/`) — if the UI changed visually
- **Unit tests** (`test/`) — update or add tests for any changes to protocol, state machine, or app logic

Do this before committing. Don't let docs or tests drift from the code.

## Common Pitfalls

- **Format specifiers on RISC-V**: `uint32_t` is `unsigned int`, not `unsigned long`. Use `%u` or cast to `unsigned`, not `%lu`.
- **Broadcast ACK**: Meshtastic nodes don't ACK broadcast messages. `kWantAck` is off for this reason.
- **Send-on-wake** (`kSendOnWake`): disabled by default. Uses `esp_sleep_get_wakeup_cause()` to distinguish deep sleep wake from cold boot.
- **Upload retry**: if the port is "busy", something else has it open (screen, serial monitor). Kill it first.
