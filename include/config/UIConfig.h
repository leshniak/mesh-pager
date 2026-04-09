#pragma once

/// UI layout constants and color palette.
///
/// All dimensions target the Nesso N1's 135×240 pixel ST7789 display in
/// portrait orientation. Colors use 16-bit RGB565 format — the native format
/// of the LGFX graphics library and the display controller, so no runtime
/// conversion is needed.
///
/// Layout overview (top → bottom):
///   ┌──────────────────────┐ 0
///   │    Status bar (18px) │ — node ID (left), battery (right)
///   ├──────────────────────┤ 18
///   │    Toast (optional)  │ — incoming message notification overlay
///   ├──────────────────────┤
///   │                      │
///   │    Message card       │ — bordered box with canned message text
///   │    (fills remaining)  │   progress bar (top edge), page dots (bottom)
///   │                      │
///   └──────────────────────┘ 240

#include <cstdint>
#include <cstddef>

namespace mesh::config::ui {

// ── Screen dimensions ───────────────────────────────────────────────────────
inline constexpr int16_t kScreenWidth  = 135;  ///< ST7789 width in portrait mode
inline constexpr int16_t kScreenHeight = 240;  ///< ST7789 height in portrait mode

// ── Status bar ──────────────────────────────────────────────────────────────
inline constexpr int16_t kStatusBarHeight = 18; ///< Height of the top status bar (px)

// ── Message card ────────────────────────────────────────────────────────────
inline constexpr int16_t kCardMargin  = 6;   ///< Outer margin around the card (px)
inline constexpr int16_t kCardPadding = 10;  ///< Inner padding within the card border (px)

// ── Page dots (message index indicator) ─────────────────────────────────────
inline constexpr int16_t kDotRadius   = 3;   ///< Radius of each pagination dot (px)
inline constexpr int16_t kDotSpacing  = 11;  ///< Center-to-center spacing between dots (px)

// ── Send progress bar ───────────────────────────────────────────────────────
inline constexpr int16_t kProgressBarHeight = 4; ///< Height of the hold-to-send progress bar (px)

// ── Toast notification ──────────────────────────────────────────────────────
// Toasts appear below the status bar when a message is received. They show
// the sender ID, message text, and a shrinking timer bar.

inline constexpr int16_t kToastMarginH     = 6;  ///< Horizontal margin around toast (px)
inline constexpr int16_t kToastMarginTop    = 3;  ///< Vertical gap above/below toast (px)
inline constexpr int16_t kToastPadding      = 6;  ///< Internal padding within toast box (px)
inline constexpr int16_t kToastTimerHeight  = 2;  ///< Height of the countdown timer bar (px)

// ── Timing ──────────────────────────────────────────────────────────────────
inline constexpr uint32_t kHoldToSendMs   = 1000;  ///< Touch hold duration to trigger transmit (ms)
inline constexpr uint32_t kSwipeThresholdPx = 20;  ///< Min finger displacement to register a swipe (px)
inline constexpr uint32_t kToastDurationMs = 3000;  ///< How long toast notifications stay visible (ms)
inline constexpr uint32_t kSentFlashMs     = 500;   ///< Duration of the sent/error background flash (ms)

// ── History overlay ─────────────────────────────────────────────────────────
// Swipe down opens a full-screen overlay listing recent received messages.
// Stored in a fixed-size circular buffer.

inline constexpr size_t kHistoryMaxEntries = 5;   ///< Max entries kept in the receive history ring
inline constexpr size_t kHistoryTextMaxLen = 80;   ///< Max chars stored per history entry (truncated)

// ── Color palette (16-bit RGB565) ───────────────────────────────────────────
// The palette is a dark navy/blue theme chosen for OLED-like readability on
// the ST7789 TFT. Send feedback uses teal tones (not green) to stay cohesive.

/// Convert 8-bit RGB components to a 16-bit RGB565 value at compile time.
/// RGB565 packs color as: RRRRRGGG GGGBBBBB (5-6-5 bits).
inline constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

inline constexpr uint16_t kColorBackground    = rgb565(0x1a, 0x1a, 0x2e); ///< Main background (dark navy)
inline constexpr uint16_t kColorStatusBar     = rgb565(0x0d, 0x11, 0x17); ///< Status bar background
inline constexpr uint16_t kColorStatusText    = rgb565(0x8b, 0x94, 0x9e); ///< Status bar text (muted gray)
inline constexpr uint16_t kColorAccentBlue    = rgb565(0x58, 0xa6, 0xff); ///< Channel name, active dot, sender IDs
inline constexpr uint16_t kColorCardBg        = rgb565(0x16, 0x1b, 0x22); ///< Message card background
inline constexpr uint16_t kColorCardBorder    = rgb565(0x30, 0x36, 0x3d); ///< Card border and inactive dots
inline constexpr uint16_t kColorTextPrimary   = rgb565(0xff, 0xff, 0xff); ///< Primary text (white)
inline constexpr uint16_t kColorTextDim       = rgb565(0x48, 0x4f, 0x58); ///< Hint text and secondary labels
inline constexpr uint16_t kColorToast         = rgb565(0x1f, 0x6f, 0xeb); ///< Toast notification background (blue)
inline constexpr uint16_t kColorToastText     = rgb565(0xff, 0xff, 0xff); ///< Toast text (white)
inline constexpr uint16_t kColorSendGreen     = rgb565(0x00, 0xd2, 0xb4); ///< Send feedback: progress bar, border (vivid cyan-teal)
inline constexpr uint16_t kColorSendTextGreen = rgb565(0x5c, 0xff, 0xe0); ///< Send feedback: message text (bright mint)
inline constexpr uint16_t kColorSentFlash     = rgb565(0x0a, 0x4a, 0x42); ///< Card background during "sent" flash (medium-dark teal)
inline constexpr uint16_t kColorError         = rgb565(0xda, 0x36, 0x33); ///< Error flash and low battery (red)

}  // namespace mesh::config::ui
