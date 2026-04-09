# Touch UI Design — Canned Message Pager

**Date:** 2026-04-09
**Goal:** Replace the log-based Display HAL with a graphical touch UI on the Nesso N1's 135x240 ST7789 TFT with FT5x06 capacitive touch. Preserve all existing functionality.
**Layout:** Card + Toast Hybrid — centered message card as primary element, RX toasts, history overlay.

## Hardware

- **Display:** ST7789 IPS TFT, 135x240 pixels, portrait orientation
- **Touch:** FT5x06 capacitive, I2C 0x38, range 0-134 x 0-239, interrupt on GPIO3
- **Graphics library:** M5GFX (LGFX) via M5Unified — `M5.Display.*` for drawing, `M5.Touch.*` for input
- **Buttons:** KEY1 (hold = power off, unchanged)

## Screen Layout

Three zones, top to bottom:

### 1. Status Bar (top, ~16px height)

- Left: channel name (e.g., "Public") in accent blue
- Right: node ID + battery percentage (e.g., "!A1B2C3D4 - 87%")
- Background: dark (#0d1117), 1px bottom border
- Tappable: tap toggles RX history overlay

### 2. Message Card (center, fills remaining space)

- Dark card background (#161b22) with subtle border (#30363d), rounded corners
- Centered message text, largest font that fits (~20px equivalent for short messages)
- Page indicator dots below text — one dot per canned message, current highlighted in blue
- First-launch hint text below dots: "swipe - hold to send" in dim gray, disappears after first interaction

### 3. Toast Area (overlays between status bar and card)

- RX messages appear as blue (#1f6feb) rounded toast between status bar and message card
- Shows sender node ID (small) and message text
- Auto-dismisses after 3 seconds
- Small progress bar at toast bottom showing remaining time

## Interactions

### Swipe Left/Right — Cycle Messages

- Touch down on card area, horizontal drag > 20px threshold = swipe
- Swipe right = next message, swipe left = previous message
- Wraps around at both ends
- Card visually slides in direction of swipe (brief animation)
- Page dots update immediately
- Does NOT persist index (existing behavior: save on sleep/poweroff only)

### Hold to Send — 1 Second Press

- Touch down on card, finger stays still (< 20px movement), held for 1000ms
- Visual feedback during hold:
  - Card border color transitions to green (#238636)
  - Message text color transitions to green (#7ee787)
  - 4px green progress bar at card bottom fills left-to-right over 1000ms
- Release before 1000ms = cancel, all visual state resets immediately
- Progress bar reaches 100%:
  - Message transmitted via mesh radio
  - Card background flashes green (#0f3d1a) for 500ms, then fades back
  - TX buzzer tone plays
  - Progress bar resets
- If TX fails: card background flashes red briefly, error logged to serial

### Tap Status Bar — RX History

- Tap on status bar area (top 16px) toggles history overlay
- History overlay:
  - Semi-transparent dark background covers card area
  - Shows last 5 received messages, newest first
  - Each entry: sender node ID (blue), relative timestamp ("1m", "3m"), message text
  - "tap to close" hint at bottom
- Tap anywhere while overlay is open = close overlay
- History is stored in a circular buffer in RAM (not persisted across reboots)
- Own transmitted messages also appear in history (marked with own node ID)

### KEY1 Hold — Power Off (unchanged)

- 1 second hold on KEY1 = power off
- Existing behavior preserved exactly

## Sleep Behavior

- 15 second inactivity timeout = display sleep (existing)
- Touch or charge state change = wake display, reset timer (existing)
- While display is sleeping, any touch wakes and resets timer but does NOT trigger send/swipe
- First touch after wake is consumed as "wake" — no action taken

## Rendering Approach

Use M5GFX (LGFX) sprite-based double buffering to avoid flicker:

- **Full-screen sprite** (135x240, 16-bit color) — draw entire frame to sprite, then push to display in one DMA transfer
- Redraw only when state changes (touch event, toast timeout, message cycle) — not every loop iteration
- Keep a `dirty` flag: set on any state change, cleared after render

### Color Palette (constexpr, defined in config)

| Name | Hex | Usage |
|------|-----|-------|
| `kColorBackground` | #1a1a2e | Screen background |
| `kColorStatusBar` | #0d1117 | Status bar background |
| `kColorStatusText` | #8b949e | Status bar text |
| `kColorAccentBlue` | #58a6ff | Channel name, active dot, node IDs |
| `kColorCardBg` | #161b22 | Message card fill |
| `kColorCardBorder` | #30363d | Card border, inactive dots |
| `kColorTextPrimary` | #ffffff | Message text |
| `kColorTextDim` | #484f58 | Hints, timestamps |
| `kColorToast` | #1f6feb | RX toast background |
| `kColorSendGreen` | #238636 | Progress bar, pressing border |
| `kColorSendTextGreen` | #7ee787 | Text while pressing |
| `kColorSentFlash` | #0f3d1a | Card bg flash on send |
| `kColorError` | #da3633 | TX error flash |

## Architecture Changes

### New Files

| File | Responsibility |
|------|---------------|
| `include/config/UIConfig.h` | Color palette, layout constants, timing constants (header-only) |
| `include/hal/TouchInput.h` | Touch gesture detection: swipe, hold, tap zones |
| `src/hal/TouchInput.cpp` | Gesture state machine using M5.Touch |
| `include/ui/Renderer.h` | Sprite-based screen renderer |
| `src/ui/Renderer.cpp` | Draws status bar, message card, toasts, history, send feedback |
| `include/ui/ToastManager.h` | RX toast queue and auto-dismiss timing |
| `src/ui/ToastManager.cpp` | Circular buffer of recent messages, toast lifecycle |

### Modified Files

| File | Change |
|------|--------|
| `include/hal/Display.h` | Expand API: add render-related methods, keep sleep/wakeup |
| `src/hal/Display.cpp` | Replace log-based impl with sprite rendering orchestration |
| `src/main.cpp` | Wire touch input, renderer, toast manager into loop |
| `include/app/AppState.h` | Add touch-specific events (swipe direction, hold progress, status bar tap) |
| `src/app/AppState.cpp` | Handle new touch events in state transitions |

### Module Responsibilities

- **TouchInput** — reads M5.Touch every loop, classifies raw touch into gestures (idle, swiping, holding, tap). Outputs a `TouchEvent` struct consumed by main loop. Pure input processing — no rendering, no business logic.
- **ToastManager** — maintains circular buffer of last 5 RX messages with timestamps. Manages active toast state (visible, countdown). Called from main loop on RX and on timer tick.
- **Renderer** — takes current app state + touch state + toast state, draws the frame to a sprite, pushes to display. Stateless rendering function — given inputs, produces pixels. Owns the LGFX sprite.
- **Display HAL** — orchestrates init, sleep/wakeup, and owns the dirty flag. Calls Renderer when dirty. Serial logging kept for debugging (logInfo/logError write to serial only, not display).

## What Does NOT Change

- Mesh protocol (packet format, encryption, radio parameters)
- Power management (charging, deep sleep, peripheral power-down)
- Buzzer tones (same frequencies and patterns)
- CannedMessages class (same API, same persistence behavior)
- secrets.h format
- KEY1 power-off behavior
