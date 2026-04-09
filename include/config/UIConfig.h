#pragma once

#include <cstdint>
#include <cstddef>

namespace mesh::config::ui {

// --- Screen dimensions ---
inline constexpr int16_t kScreenWidth  = 135;
inline constexpr int16_t kScreenHeight = 240;

// --- Status bar ---
inline constexpr int16_t kStatusBarHeight = 18;

// --- Message card ---
inline constexpr int16_t kCardMargin  = 6;
inline constexpr int16_t kCardRadius  = 8;
inline constexpr int16_t kCardPadding = 10;

// --- Page dots ---
inline constexpr int16_t kDotRadius   = 3;
inline constexpr int16_t kDotSpacing  = 11;
inline constexpr int16_t kDotMarginTop = 10;

// --- Send progress bar ---
inline constexpr int16_t kProgressBarHeight = 4;

// --- Toast ---
inline constexpr int16_t kToastMarginH     = 6;
inline constexpr int16_t kToastMarginTop    = 3;
inline constexpr int16_t kToastPadding      = 6;
inline constexpr int16_t kToastRadius       = 6;
inline constexpr int16_t kToastTimerHeight  = 2;

// --- Timing ---
inline constexpr uint32_t kHoldToSendMs   = 1000;
inline constexpr uint32_t kSwipeThresholdPx = 20;
inline constexpr uint32_t kToastDurationMs = 3000;
inline constexpr uint32_t kSentFlashMs     = 500;

// --- History ---
inline constexpr size_t kHistoryMaxEntries = 5;
inline constexpr size_t kHistoryTextMaxLen = 80;

// --- Color palette (16-bit RGB565) ---
inline constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

inline constexpr uint16_t kColorBackground    = rgb565(0x1a, 0x1a, 0x2e);
inline constexpr uint16_t kColorStatusBar     = rgb565(0x0d, 0x11, 0x17);
inline constexpr uint16_t kColorStatusText    = rgb565(0x8b, 0x94, 0x9e);
inline constexpr uint16_t kColorAccentBlue    = rgb565(0x58, 0xa6, 0xff);
inline constexpr uint16_t kColorCardBg        = rgb565(0x16, 0x1b, 0x22);
inline constexpr uint16_t kColorCardBorder    = rgb565(0x30, 0x36, 0x3d);
inline constexpr uint16_t kColorTextPrimary   = rgb565(0xff, 0xff, 0xff);
inline constexpr uint16_t kColorTextDim       = rgb565(0x48, 0x4f, 0x58);
inline constexpr uint16_t kColorToast         = rgb565(0x1f, 0x6f, 0xeb);
inline constexpr uint16_t kColorToastText     = rgb565(0xff, 0xff, 0xff);
inline constexpr uint16_t kColorSendGreen     = rgb565(0x23, 0x86, 0x36);
inline constexpr uint16_t kColorSendTextGreen = rgb565(0x7e, 0xe7, 0x87);
inline constexpr uint16_t kColorSentFlash     = rgb565(0x0f, 0x3d, 0x1a);
inline constexpr uint16_t kColorError         = rgb565(0xda, 0x36, 0x33);

}  // namespace mesh::config::ui
