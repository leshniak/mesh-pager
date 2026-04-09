# mesh-pager — Claude Code Guide

## Build & Upload

```bash
# PlatformIO binary (not in PATH)
~/.platformio/penv/bin/pio run                    # build
~/.platformio/penv/bin/pio run -t upload --upload-port /dev/tty.usbmodem1101  # flash
```

**Upload port is always `/dev/tty.usbmodem1101`** — never auto-detect or change it.

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

## Display

- 135x240 pixels, portrait orientation, 16-bit color
- Single `LGFX_Sprite` double-buffered to internal SRAM (no PSRAM on ESP32-C6)
- Layout top-to-bottom: status bar (18px) → toast (if active) → message card (fills rest)
- Status bar: node ID (left), battery icon + percentage (right)
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

## Secrets

`include/secrets.h` contains channel name, base64 key, and canned message array. Never commit it. See `secrets.example.h` for the template.

## Common Pitfalls

- **Format specifiers on RISC-V**: `uint32_t` is `unsigned int`, not `unsigned long`. Use `%u` or cast to `unsigned`, not `%lu`.
- **Broadcast ACK**: Meshtastic nodes don't ACK broadcast messages. `kWantAck` is off for this reason.
- **Send-on-wake** (`kSendOnWake`): disabled by default. Uses `esp_sleep_get_wakeup_cause()` to distinguish deep sleep wake from cold boot.
- **Upload retry**: if the port is "busy", something else has it open (screen, serial monitor). Kill it first.
