#include "ui/Renderer.h"
#include "ui/ToastManager.h"
#include "config/UIConfig.h"

#include <M5Unified.h>
#include <cstdio>
#include <cstring>

namespace mesh::ui {

using namespace mesh::config::ui;

/// Allocate the off-screen sprite in internal SRAM. ESP32-C6 has no PSRAM,
/// so we explicitly disable PSRAM allocation. The sprite holds one full frame
/// (135×240×2 bytes = 64,800 bytes) in RGB565 format.
void Renderer::init() {
    sprite_.setPsram(false);
    sprite_.setColorDepth(16);
    sprite_.createSprite(kScreenWidth, kScreenHeight);
}

/// Render one complete frame and push it to the display.
///
/// Draw order matters for layering:
///   1. Background fill (clears previous frame)
///   2. Status bar (always visible)
///   3. Toast notification (pushes message card down if active)
///   4. Message card (fills remaining space)
///   5. History overlay (drawn ON TOP of everything if active)
///   6. pushSprite() — single DMA transfer to display (no flicker)
void Renderer::render(const RenderState& state) {
    sprite_.fillScreen(kColorBackground);

    drawStatusBar(state, 0);

    // Toast notification appears between status bar and message card.
    // contentTop is adjusted downward if a toast is visible.
    int16_t contentTop = kStatusBarHeight;
    if (state.toastManager) {
        drawToast(state, kStatusBarHeight, contentTop);
    }

    drawMessageCard(state, contentTop, kScreenHeight);

    // History overlay covers everything below the status bar
    if (state.showHistory && state.toastManager) {
        drawHistory(state, kStatusBarHeight);
    }

    sprite_.pushSprite(&M5.Display, 0, 0);
}

/// Draw the status bar: node ID on the left, battery icon + percentage on the right.
/// The battery icon is a small rectangle outline with a nub on the left side
/// (mimicking a battery shape). The fill color changes to red when ≤20%.
void Renderer::drawStatusBar(const RenderState& state, int16_t y) {
    sprite_.fillRect(0, y, kScreenWidth, kStatusBarHeight, kColorStatusBar);
    sprite_.drawFastHLine(0, y + kStatusBarHeight - 1, kScreenWidth, kColorCardBorder);

    sprite_.setTextSize(1);

    // Node ID (left-aligned): Meshtastic convention is "!XXXXXXXX"
    char idBuf[12];
    snprintf(idBuf, sizeof(idBuf), "!%08X", state.nodeId);
    sprite_.setTextColor(kColorStatusText);
    sprite_.setTextDatum(middle_left);
    sprite_.drawString(idBuf, 4, y + kStatusBarHeight / 2);

    // Battery icon (right-aligned): outline + positive terminal nub + fill bar
    const int16_t batW = 12, batH = 7, nubW = 2, nubH = 3;
    const int16_t batX = kScreenWidth - 4 - batW;
    const int16_t batY = y + (kStatusBarHeight - batH) / 2;
    sprite_.drawRect(batX, batY, batW, batH, kColorStatusText);
    sprite_.fillRect(batX - nubW, batY + (batH - nubH) / 2, nubW, nubH, kColorStatusText);

    // Fill proportional to battery level; red if critically low
    const int16_t fillW = static_cast<int16_t>((batW - 2) * state.batteryPercent / 100);
    uint16_t batColor = (state.batteryPercent > 20) ? kColorSendGreen : kColorError;
    if (fillW > 0) {
        sprite_.fillRect(batX + 1, batY + 1, fillW, batH - 2, batColor);
    }

    // Battery percentage text (right-aligned, left of the icon)
    char pctBuf[6];
    snprintf(pctBuf, sizeof(pctBuf), "%d%%", state.batteryPercent);
    sprite_.setTextDatum(middle_right);
    const int16_t pctRight = batX - nubW - 2;
    sprite_.drawString(pctBuf, pctRight, y + kStatusBarHeight / 2);

    // Stay-awake indicator: red dot left of battery percentage
    if (state.stayAwake) {
        const int16_t dotX = pctRight - sprite_.textWidth(pctBuf) - 5;
        sprite_.fillCircle(dotX, y + kStatusBarHeight / 2, 2, kColorError);
    }
}

/// Draw the toast notification banner below the status bar.
///
/// Layout (within the toast box):
///   ┌─────────────────────────────────┐
///   │ padding                         │
///   │ !SENDER_ID          (light blue)│
///   │ 2px gap                         │
///   │ Message text...      (white)    │
///   │ padding                         │
///   │ ████████░░░░░  (countdown bar)  │
///   └─────────────────────────────────┘
///
/// The countdown bar shrinks from left to right over kToastDurationMs.
/// Long message text is truncated with ".." via drawTruncated().
///
/// Sets bottomY to the Y coordinate below the toast (used by drawMessageCard
/// to know where the card area starts).
void Renderer::drawToast(const RenderState& state, int16_t y, int16_t& bottomY) {
    bottomY = y;
    if (!state.toastManager || !state.toastManager->hasActiveToast()) return;

    const auto& toast = state.toastManager->activeToast();
    const float progress = state.toastManager->toastProgress(state.nowMs);

    const int16_t toastX = kToastMarginH;
    const int16_t toastY = y + kToastMarginTop;
    const int16_t toastW = kScreenWidth - 2 * kToastMarginH;

    sprite_.setTextSize(1);
    const int16_t lineH = sprite_.fontHeight();
    const int16_t toastH = kToastPadding + lineH + 2 + lineH + kToastPadding + kToastTimerHeight;

    sprite_.fillRect(toastX, toastY, toastW, toastH, kColorToast);

    // Sender ID in light blue (left), RSSI/SNR in dim white (right)
    char senderBuf[16];
    snprintf(senderBuf, sizeof(senderBuf), "!%08X", toast.sender);
    sprite_.setTextColor(rgb565(0xb0, 0xc4, 0xde));
    sprite_.setTextDatum(top_left);
    sprite_.drawString(senderBuf, toastX + kToastPadding, toastY + kToastPadding);

    if (toast.snr != 0) {
        char snrBuf[10];
        snprintf(snrBuf, sizeof(snrBuf), "%ddB", toast.snr);
        sprite_.setTextColor(rgb565(0xb0, 0xc4, 0xde));
        sprite_.setTextDatum(top_right);
        sprite_.drawString(snrBuf, toastX + toastW - kToastPadding, toastY + kToastPadding);
    }

    // Message text (truncated with ".." if too long for one line)
    sprite_.setTextDatum(top_left);
    drawTruncated(toast.text, toastX + kToastPadding,
                  toastY + kToastPadding + lineH + 2,
                  toastW - 2 * kToastPadding, kColorToastText);

    // Countdown timer bar: white bar that shrinks from right to left
    const int16_t timerW = static_cast<int16_t>((1.0f - progress) * toastW);
    if (timerW > 0) {
        sprite_.fillRect(toastX, toastY + toastH - kToastTimerHeight,
                         timerW, kToastTimerHeight,
                         rgb565(0xff, 0xff, 0xff));
    }

    bottomY = toastY + toastH + kToastMarginTop;
}

/// Draw the main message card — the central UI element.
///
/// Card layout (top → bottom within the bordered rectangle):
///   1. "swipe | hold" hint (shown until first interaction, then hidden)
///   2. Channel name in accent blue
///   3. Canned message text (auto-sized, word-wrapped, centered)
///   4. Page dots (one per canned message, active dot highlighted)
///
/// Visual feedback overlays:
///   - Hold-to-send progress bar: teal bar grows left-to-right at top of card
///   - Sent flash: card background briefly turns dark teal
///   - Error flash: card background briefly turns red
///   - Holding: border turns teal, text turns light teal
void Renderer::drawMessageCard(const RenderState& state, int16_t top, int16_t bottom) {
    const int16_t cardX = kCardMargin;
    const int16_t cardY = top + kCardMargin;
    const int16_t cardW = kScreenWidth - 2 * kCardMargin;
    const int16_t cardH = bottom - top - 2 * kCardMargin;

    // Background: normal → teal flash (sent) → red flash (error)
    uint16_t bgColor = kColorCardBg;
    if (state.sentFlash) bgColor = kColorSentFlash;
    else if (state.errorFlash) bgColor = kColorError;
    sprite_.fillRect(cardX, cardY, cardW, cardH, bgColor);

    // Border: teal while holding or after send, default gray otherwise
    uint16_t borderColor = kColorCardBorder;
    if (state.isHolding) borderColor = kColorSendGreen;
    else if (state.sentFlash) borderColor = kColorSendGreen;
    sprite_.drawRect(cardX, cardY, cardW, cardH, borderColor);

    // Text color: light teal during hold, white otherwise
    uint16_t textColor = kColorTextPrimary;
    if (state.isHolding) textColor = kColorSendTextGreen;

    // Layout elements top-down within the card
    int16_t contentTop = cardY + kCardPadding;

    // Hint text (shown until the user's first swipe or hold)
    if (state.showHint) {
        sprite_.setTextSize(1);
        sprite_.setTextColor(kColorTextDim);
        sprite_.setTextDatum(top_center);
        sprite_.drawString("swipe | hold",
                           kScreenWidth / 2, contentTop);
        contentTop += sprite_.fontHeight() + 5;  // 5px gap to channel name
    }

    // Channel name (always visible)
    sprite_.setTextSize(1);
    sprite_.setTextColor(kColorAccentBlue);
    sprite_.setTextDatum(top_center);
    sprite_.drawString(state.channelName ? state.channelName : "",
                       kScreenWidth / 2, contentTop);
    contentTop += sprite_.fontHeight() + 4;

    // Page dots at bottom of card — center the message text between
    // contentTop and the dots
    const int16_t dotsCenter = cardY + cardH - kCardPadding - kDotRadius;
    const int16_t textCenterY = contentTop + (dotsCenter - kDotRadius - 4 - contentTop) / 2;
    const int16_t textMaxH = dotsCenter - kDotRadius - 4 - contentTop;

    // Message text: starts at size 2, auto-reduces and word-wraps if needed
    sprite_.setTextSize(2);
    drawCenteredText(state.messageText ? state.messageText : "",
                     kScreenWidth / 2, textCenterY,
                     cardW - 2 * kCardPadding, textMaxH, textColor);

    drawPageDots(kScreenWidth / 2, dotsCenter, state.messageCount, state.messageIndex);

    // Hold-to-send progress bar: teal bar at the top edge of the card
    if (state.isHolding && state.holdProgress > 0.0f) {
        const int16_t barW = static_cast<int16_t>(state.holdProgress * cardW);
        if (barW > 0) {
            sprite_.fillRect(cardX, cardY, barW, kProgressBarHeight, kColorSendGreen);
        }
    }
}

/// Draw a horizontal row of pagination dots centered at (cx, y).
/// The active dot is drawn in accent blue; inactive dots use the muted border color.
void Renderer::drawPageDots(int16_t cx, int16_t y, uint8_t count, uint8_t active) {
    if (count == 0) return;

    const int16_t totalW = (count - 1) * kDotSpacing;
    int16_t x = cx - totalW / 2;

    for (uint8_t i = 0; i < count; ++i) {
        const uint16_t color = (i == active) ? kColorAccentBlue : kColorCardBorder;
        sprite_.fillCircle(x, y, kDotRadius, color);
        x += kDotSpacing;
    }
}

/// Draw single-line text, truncating with ".." if it exceeds maxWidth.
/// Works by progressively shortening the string and replacing the last two
/// characters with dots until the rendered width fits.
/// Uses a 128-byte stack buffer — input longer than that is pre-truncated.
void Renderer::drawTruncated(const char* text, int16_t x, int16_t y,
                             int16_t maxWidth, uint16_t color) {
    sprite_.setTextColor(color);
    if (sprite_.textWidth(text) <= maxWidth) {
        sprite_.drawString(text, x, y);
        return;
    }
    char buf[128];
    size_t len = strlen(text);
    if (len >= sizeof(buf) - 1) len = sizeof(buf) - 4;
    memcpy(buf, text, len);
    buf[len] = '\0';
    while (len > 0) {
        buf[len] = '\0';
        buf[len > 0 ? len - 1 : 0] = '.';
        if (len > 1) buf[len - 2] = '.';
        if (sprite_.textWidth(buf) <= maxWidth) break;
        --len;
    }
    sprite_.drawString(buf, x, y);
}

/// Draw multi-line centered text with automatic font size reduction and word-wrapping.
///
/// Algorithm:
///   1. Start at text size 2 (set by caller). If the text is wider than maxWidth,
///      try 1.5, then 1. This gives canned messages the largest readable size.
///   2. Word-wrap the text into up to 8 lines, breaking at spaces when possible.
///      If a word is too long for any line, it's force-broken mid-word.
///   3. If the text overflows the available lines (maxHeight / lineHeight), the
///      last line is truncated with ".." to indicate continuation.
///   4. Lines are vertically centered around cy.
///
/// Note: LGFX has no built-in text clipping — we must handle wrapping and
/// truncation manually. This function is the primary defense against long
/// canned messages or received text overflowing the card area.
void Renderer::drawCenteredText(const char* text, int16_t cx, int16_t cy,
                                int16_t maxWidth, int16_t maxHeight,
                                uint16_t color) {
    sprite_.setTextColor(color);
    sprite_.setTextDatum(middle_center);

    // Progressive font size reduction: 2 → 1.5 → 1
    if (sprite_.textWidth(text) > maxWidth) {
        sprite_.setTextSize(1.5f);
        if (sprite_.textWidth(text) > maxWidth) {
            sprite_.setTextSize(1);
        }
    }

    const int16_t lineHeight = sprite_.fontHeight();
    const int maxLines = (maxHeight > 0) ? (maxHeight / lineHeight) : 8;

    // Word-wrap into fixed-size line buffers
    char lines[8][32];
    int lineCount = 0;
    const char* p = text;

    while (*p && lineCount < maxLines) {
        while (*p == ' ') ++p;  // Skip leading whitespace
        if (!*p) break;

        // Probe characters one at a time, tracking the best word-break position
        size_t bestBreak = 0;
        char probe[128];
        size_t i = 0;
        for (; p[i] && i < sizeof(probe) - 1; ++i) {
            if (p[i] == '\n') { bestBreak = i; break; }  // Explicit line break
            probe[i] = p[i];
            probe[i + 1] = '\0';
            if (sprite_.textWidth(probe) <= maxWidth) {
                // Good break points: at spaces or end of text
                if (p[i] == ' ' || p[i + 1] == '\0' || p[i + 1] == ' ')
                    bestBreak = i + 1;
            } else {
                break;  // Exceeded width — stop adding characters
            }
        }

        if (bestBreak == 0) bestBreak = (i > 0) ? i : 1;  // Force-break if no space found

        size_t copyLen = bestBreak;
        if (copyLen >= sizeof(lines[0])) copyLen = sizeof(lines[0]) - 1;

        // If this is the last line and text continues, truncate with ".."
        if (lineCount == maxLines - 1 && p[bestBreak] != '\0' && p[bestBreak] != '\n') {
            memcpy(lines[lineCount], p, copyLen);
            lines[lineCount][copyLen] = '\0';
            if (copyLen >= 2) {
                lines[lineCount][copyLen - 1] = '.';
                lines[lineCount][copyLen - 2] = '.';
            }
        } else {
            memcpy(lines[lineCount], p, copyLen);
            lines[lineCount][copyLen] = '\0';
        }

        ++lineCount;
        p += bestBreak;
        if (*p == '\n') ++p;
    }

    // Vertically center the block of lines around cy
    int16_t startY = cy - (lineCount - 1) * lineHeight / 2;
    for (int i = 0; i < lineCount; ++i) {
        sprite_.drawString(lines[i], cx, startY + i * lineHeight);
    }
}

/// Draw the full-screen message history overlay (triggered by swipe-down).
///
/// Covers everything below the status bar with a dark background. Lists the
/// most recent received messages (newest first) from the ToastManager's
/// circular history buffer. Each entry shows:
///   - Sender ID (left, accent blue) + relative time (right, dim)
///   - Message text (truncated with ".." if too long)
///   - Dotted separator line
///
/// A "swipe up to close" hint is shown at the bottom of the list.
void Renderer::drawHistory(const RenderState& state, int16_t top) {
    if (!state.toastManager) return;

    const size_t count = state.toastManager->historyCount();
    if (count == 0) return;

    constexpr int16_t kPad = 6;          // Inner padding
    constexpr int16_t kSenderRowH = 12;  // Height of sender ID row
    constexpr int16_t kTextRowH = 14;    // Height of message text row
    constexpr int16_t kSepDotPitch = 4;  // Pixel spacing between separator dots
    constexpr int16_t kSepH = 4;         // Vertical space for separator line

    // Dark overlay covering the entire area below the status bar
    const int16_t overlayH = kScreenHeight - top;
    sprite_.fillRect(0, top, kScreenWidth, overlayH, kColorStatusBar);

    sprite_.setTextSize(1);
    int16_t y = top + kPad;

    for (size_t i = 0; i < count && i < kHistoryMaxEntries; ++i) {
        const auto& entry = state.toastManager->historyAt(i);
        if (!entry.valid) continue;

        // Sender ID (left-aligned)
        char senderBuf[16];
        snprintf(senderBuf, sizeof(senderBuf), "!%08X", entry.sender);
        sprite_.setTextColor(kColorAccentBlue);
        sprite_.setTextDatum(top_left);
        sprite_.drawString(senderBuf, kPad, y);

        // SNR + relative time (right-aligned, e.g. "3dB <1m")
        const uint32_t agoMs = state.nowMs - entry.timestampMs;
        const uint32_t agoMin = agoMs / 60000;
        char infoBuf[16];
        if (entry.snr != 0) {
            if (agoMin < 1)
                snprintf(infoBuf, sizeof(infoBuf), "%ddB <1m", entry.snr);
            else
                snprintf(infoBuf, sizeof(infoBuf), "%ddB %lum", entry.snr, (unsigned long)agoMin);
        } else {
            if (agoMin < 1)
                snprintf(infoBuf, sizeof(infoBuf), "<1m");
            else
                snprintf(infoBuf, sizeof(infoBuf), "%lum", (unsigned long)agoMin);
        }
        sprite_.setTextColor(kColorTextDim);
        sprite_.setTextDatum(top_right);
        sprite_.drawString(infoBuf, kScreenWidth - kPad, y);

        // Message text (truncated if too long)
        y += kSenderRowH;
        sprite_.setTextDatum(top_left);
        drawTruncated(entry.text, kPad, y,
                      kScreenWidth - 2 * kPad, kColorTextPrimary);

        // Dotted separator line between entries
        y += kTextRowH;
        for (int16_t dx = kPad; dx < kScreenWidth - kPad; dx += kSepDotPitch) {
            sprite_.drawPixel(dx, y, kColorCardBorder);
        }
        y += kSepH;
    }

    // Dismiss hint at the bottom
    sprite_.setTextColor(kColorTextDim);
    sprite_.setTextDatum(top_center);
    sprite_.drawString("swipe up to close", kScreenWidth / 2, y + 2);
}

}  // namespace mesh::ui
