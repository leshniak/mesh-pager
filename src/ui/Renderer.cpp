#include "ui/Renderer.h"
#include "ui/ToastManager.h"
#include "config/UIConfig.h"

#include <M5Unified.h>
#include <cstdio>
#include <cstring>

namespace mesh::ui {

using namespace mesh::config::ui;

void Renderer::init() {
    sprite_.setPsram(false); // ESP32-C6 has no PSRAM; ensure sprite uses internal SRAM
    sprite_.setColorDepth(16);
    sprite_.createSprite(kScreenWidth, kScreenHeight);
}

void Renderer::render(const RenderState& state) {
    sprite_.fillScreen(kColorBackground);

    drawStatusBar(state, 0);

    int16_t contentTop = kStatusBarHeight;
    if (state.toastManager) {
        drawToast(state, kStatusBarHeight, contentTop);
    }

    drawMessageCard(state, contentTop, kScreenHeight);

    if (state.showHistory && state.toastManager) {
        drawHistory(state, kStatusBarHeight);
    }

    sprite_.pushSprite(&M5.Display, 0, 0);
}

void Renderer::drawStatusBar(const RenderState& state, int16_t y) {
    sprite_.fillRect(0, y, kScreenWidth, kStatusBarHeight, kColorStatusBar);
    sprite_.drawFastHLine(0, y + kStatusBarHeight - 1, kScreenWidth, kColorCardBorder);

    sprite_.setTextSize(1);

    char idBuf[12];
    snprintf(idBuf, sizeof(idBuf), "!%08X", state.nodeId);
    sprite_.setTextColor(kColorStatusText);
    sprite_.setTextDatum(middle_left);
    sprite_.drawString(idBuf, 4, y + kStatusBarHeight / 2);

    // Battery icon (small rect outline + nub) + percentage
    const int16_t batW = 12, batH = 7, nubW = 2, nubH = 3;
    const int16_t batX = kScreenWidth - 4 - batW;
    const int16_t batY = y + (kStatusBarHeight - batH) / 2;
    sprite_.drawRect(batX, batY, batW, batH, kColorStatusText);
    sprite_.fillRect(batX - nubW, batY + (batH - nubH) / 2, nubW, nubH, kColorStatusText);
    const int16_t fillW = static_cast<int16_t>((batW - 2) * state.batteryPercent / 100);
    uint16_t batColor = (state.batteryPercent > 20) ? kColorSendGreen : kColorError;
    if (fillW > 0) {
        sprite_.fillRect(batX + 1, batY + 1, fillW, batH - 2, batColor);
    }

    char pctBuf[6];
    snprintf(pctBuf, sizeof(pctBuf), "%d%%", state.batteryPercent);
    sprite_.setTextDatum(middle_right);
    sprite_.drawString(pctBuf, batX - nubW - 2, y + kStatusBarHeight / 2);
}

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

    char senderBuf[16];
    snprintf(senderBuf, sizeof(senderBuf), "!%08X", toast.sender);
    sprite_.setTextColor(rgb565(0xb0, 0xc4, 0xde));
    sprite_.setTextDatum(top_left);
    sprite_.drawString(senderBuf, toastX + kToastPadding, toastY + kToastPadding);

    sprite_.setTextDatum(top_left);
    drawTruncated(toast.text, toastX + kToastPadding,
                  toastY + kToastPadding + lineH + 2,
                  toastW - 2 * kToastPadding, kColorToastText);

    const int16_t timerW = static_cast<int16_t>((1.0f - progress) * toastW);
    if (timerW > 0) {
        sprite_.fillRect(toastX, toastY + toastH - kToastTimerHeight,
                         timerW, kToastTimerHeight,
                         rgb565(0xff, 0xff, 0xff));
    }

    bottomY = toastY + toastH + kToastMarginTop;
}

void Renderer::drawMessageCard(const RenderState& state, int16_t top, int16_t bottom) {
    const int16_t cardX = kCardMargin;
    const int16_t cardY = top + kCardMargin;
    const int16_t cardW = kScreenWidth - 2 * kCardMargin;
    const int16_t cardH = bottom - top - 2 * kCardMargin;

    uint16_t bgColor = kColorCardBg;
    if (state.sentFlash) bgColor = kColorSentFlash;
    else if (state.errorFlash) bgColor = kColorError;
    sprite_.fillRect(cardX, cardY, cardW, cardH, bgColor);

    uint16_t borderColor = kColorCardBorder;
    if (state.isHolding) borderColor = kColorSendGreen;
    else if (state.sentFlash) borderColor = kColorSendGreen;
    sprite_.drawRect(cardX, cardY, cardW, cardH, borderColor);

    uint16_t textColor = kColorTextPrimary;
    if (state.isHolding) textColor = kColorSendTextGreen;

    // Layout top-down: hint (optional), channel, centered text, dots at bottom
    int16_t contentTop = cardY + kCardPadding;

    if (state.showHint) {
        sprite_.setTextSize(1);
        sprite_.setTextColor(kColorTextDim);
        sprite_.setTextDatum(top_center);
        sprite_.drawString("swipe | hold",
                           kScreenWidth / 2, contentTop);
        contentTop += sprite_.fontHeight() + 5;
    }

    // Channel name
    sprite_.setTextSize(1);
    sprite_.setTextColor(kColorAccentBlue);
    sprite_.setTextDatum(top_center);
    sprite_.drawString(state.channelName ? state.channelName : "",
                       kScreenWidth / 2, contentTop);
    contentTop += sprite_.fontHeight() + 4;

    const int16_t dotsCenter = cardY + cardH - kCardPadding - kDotRadius;
    const int16_t textCenterY = contentTop + (dotsCenter - kDotRadius - 4 - contentTop) / 2;

    const int16_t textMaxH = dotsCenter - kDotRadius - 4 - contentTop;
    sprite_.setTextSize(2);
    drawCenteredText(state.messageText ? state.messageText : "",
                     kScreenWidth / 2, textCenterY,
                     cardW - 2 * kCardPadding, textMaxH, textColor);

    drawPageDots(kScreenWidth / 2, dotsCenter, state.messageCount, state.messageIndex);

    if (state.isHolding && state.holdProgress > 0.0f) {
        const int16_t barW = static_cast<int16_t>(state.holdProgress * cardW);
        if (barW > 0) {
            sprite_.fillRect(cardX, cardY, barW, kProgressBarHeight, kColorSendGreen);
        }
    }
}

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

void Renderer::drawCenteredText(const char* text, int16_t cx, int16_t cy,
                                int16_t maxWidth, int16_t maxHeight,
                                uint16_t color) {
    sprite_.setTextColor(color);
    sprite_.setTextDatum(middle_center);

    // Auto-reduce text size if too wide for a single line
    if (sprite_.textWidth(text) > maxWidth) {
        sprite_.setTextSize(1.5f);
        if (sprite_.textWidth(text) > maxWidth) {
            sprite_.setTextSize(1);
        }
    }

    const int16_t lineHeight = sprite_.fontHeight();
    const int maxLines = (maxHeight > 0) ? (maxHeight / lineHeight) : 8;

    // Word-wrap into lines
    char lines[8][32];
    int lineCount = 0;
    const char* p = text;

    while (*p && lineCount < maxLines) {
        // Skip leading spaces
        while (*p == ' ') ++p;
        if (!*p) break;

        // Find how many chars fit on this line
        size_t bestBreak = 0;
        char probe[128];
        size_t i = 0;
        for (; p[i] && i < sizeof(probe) - 1; ++i) {
            if (p[i] == '\n') { bestBreak = i; break; }
            probe[i] = p[i];
            probe[i + 1] = '\0';
            if (sprite_.textWidth(probe) <= maxWidth) {
                if (p[i] == ' ' || p[i + 1] == '\0' || p[i + 1] == ' ')
                    bestBreak = i + 1;
            } else {
                break;
            }
        }

        if (bestBreak == 0) bestBreak = (i > 0) ? i : 1;

        size_t copyLen = bestBreak;
        if (copyLen >= sizeof(lines[0])) copyLen = sizeof(lines[0]) - 1;

        // If this is the last allowed line and there's more text, truncate with ..
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

    int16_t startY = cy - (lineCount - 1) * lineHeight / 2;
    for (int i = 0; i < lineCount; ++i) {
        sprite_.drawString(lines[i], cx, startY + i * lineHeight);
    }
}

void Renderer::drawHistory(const RenderState& state, int16_t top) {
    if (!state.toastManager) return;

    const size_t count = state.toastManager->historyCount();
    if (count == 0) return;

    constexpr int16_t kPad = 6;
    constexpr int16_t kSenderRowH = 12;
    constexpr int16_t kTextRowH = 14;
    constexpr int16_t kSepDotPitch = 4;
    constexpr int16_t kSepH = 4;

    const int16_t overlayH = kScreenHeight - top;
    sprite_.fillRect(0, top, kScreenWidth, overlayH, kColorStatusBar);

    sprite_.setTextSize(1);
    int16_t y = top + kPad;

    for (size_t i = 0; i < count && i < kHistoryMaxEntries; ++i) {
        const auto& entry = state.toastManager->historyAt(i);
        if (!entry.valid) continue;

        char senderBuf[16];
        snprintf(senderBuf, sizeof(senderBuf), "!%08X", entry.sender);
        sprite_.setTextColor(kColorAccentBlue);
        sprite_.setTextDatum(top_left);
        sprite_.drawString(senderBuf, kPad, y);

        const uint32_t agoMs = state.nowMs - entry.timestampMs;
        const uint32_t agoMin = agoMs / 60000;
        char timeBuf[8];
        if (agoMin < 1) {
            snprintf(timeBuf, sizeof(timeBuf), "<1m");
        } else {
            snprintf(timeBuf, sizeof(timeBuf), "%lum", (unsigned long)agoMin);
        }
        sprite_.setTextColor(kColorTextDim);
        sprite_.setTextDatum(top_right);
        sprite_.drawString(timeBuf, kScreenWidth - kPad, y);

        y += kSenderRowH;
        sprite_.setTextDatum(top_left);
        drawTruncated(entry.text, kPad, y,
                      kScreenWidth - 2 * kPad, kColorTextPrimary);

        y += kTextRowH;

        for (int16_t dx = kPad; dx < kScreenWidth - kPad; dx += kSepDotPitch) {
            sprite_.drawPixel(dx, y, kColorCardBorder);
        }
        y += kSepH;
    }

    sprite_.setTextColor(kColorTextDim);
    sprite_.setTextDatum(top_center);
    sprite_.drawString("swipe up to close", kScreenWidth / 2, y + 2);
}

}  // namespace mesh::ui
