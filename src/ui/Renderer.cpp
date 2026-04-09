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

    sprite_.setTextDatum(middle_left);
    sprite_.setTextSize(1);

    sprite_.setTextColor(kColorAccentBlue);
    sprite_.drawString(state.channelName ? state.channelName : "",
                       4, y + kStatusBarHeight / 2);

    char rightBuf[24];
    snprintf(rightBuf, sizeof(rightBuf), "!%08X %d%%",
             state.nodeId, state.batteryPercent);
    sprite_.setTextColor(kColorStatusText);
    sprite_.setTextDatum(middle_right);
    sprite_.drawString(rightBuf, kScreenWidth - 4, y + kStatusBarHeight / 2);
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

    sprite_.fillRoundRect(toastX, toastY, toastW, toastH, kToastRadius, kColorToast);

    char senderBuf[16];
    snprintf(senderBuf, sizeof(senderBuf), "!%08X", toast.sender);
    sprite_.setTextColor(rgb565(0xb0, 0xc4, 0xde));
    sprite_.setTextDatum(top_left);
    sprite_.drawString(senderBuf, toastX + kToastPadding, toastY + kToastPadding);

    sprite_.setTextColor(kColorToastText);
    sprite_.drawString(toast.text, toastX + kToastPadding,
                       toastY + kToastPadding + lineH + 2);

    const int16_t timerW = static_cast<int16_t>(
        (1.0f - progress) * (toastW - 2 * kToastRadius));
    if (timerW > 0) {
        sprite_.fillRect(toastX + kToastRadius,
                         toastY + toastH - kToastTimerHeight,
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
    sprite_.fillRoundRect(cardX, cardY, cardW, cardH, kCardRadius, bgColor);

    uint16_t borderColor = kColorCardBorder;
    if (state.isHolding) borderColor = kColorSendGreen;
    else if (state.sentFlash) borderColor = kColorSendGreen;
    sprite_.drawRoundRect(cardX, cardY, cardW, cardH, kCardRadius, borderColor);

    uint16_t textColor = kColorTextPrimary;
    if (state.isHolding) textColor = kColorSendTextGreen;

    const int16_t dotsY = cardY + cardH - kCardMargin - kDotRadius * 2
                          - (state.showHint ? 16 : 0)
                          - kProgressBarHeight;
    const int16_t textCenterY = cardY + (dotsY - cardY) / 2;

    sprite_.setTextSize(2);
    drawCenteredText(state.messageText ? state.messageText : "",
                     kScreenWidth / 2, textCenterY,
                     cardW - 2 * kCardPadding, textColor);

    const int16_t dotsBaseline = dotsY + kDotMarginTop;
    drawPageDots(kScreenWidth / 2, dotsBaseline, state.messageCount, state.messageIndex);

    if (state.showHint) {
        sprite_.setTextSize(1);
        sprite_.setTextColor(kColorTextDim);
        sprite_.setTextDatum(top_center);
        sprite_.drawString("swipe | hold to send",
                           kScreenWidth / 2, dotsBaseline + kDotRadius * 2 + 6);
    }

    if (state.isHolding && state.holdProgress > 0.0f) {
        const int16_t barY = cardY + cardH - kProgressBarHeight;
        const int16_t barW = static_cast<int16_t>(
            state.holdProgress * (cardW - 2));
        if (barW > 0) {
            sprite_.fillRect(cardX + 1, barY, barW, kProgressBarHeight, kColorSendGreen);
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

void Renderer::drawCenteredText(const char* text, int16_t cx, int16_t cy,
                                int16_t maxWidth, uint16_t color) {
    sprite_.setTextColor(color);
    sprite_.setTextDatum(middle_center);

    const char* lineStart = text;
    const int16_t lineHeight = sprite_.fontHeight();
    int lineCount = 1;

    for (const char* p = text; *p; ++p) {
        if (*p == '\n') ++lineCount;
    }

    int16_t startY = cy - (lineCount - 1) * lineHeight / 2;

    char lineBuf[128];
    int currentLine = 0;
    for (const char* p = text; ; ++p) {
        if (*p == '\n' || *p == '\0') {
            const size_t len = static_cast<size_t>(p - lineStart);
            if (len < sizeof(lineBuf)) {
                memcpy(lineBuf, lineStart, len);
                lineBuf[len] = '\0';
                sprite_.drawString(lineBuf, cx, startY + currentLine * lineHeight);
            }
            ++currentLine;
            lineStart = p + 1;
            if (*p == '\0') break;
        }
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
        sprite_.setTextColor(kColorTextPrimary);
        sprite_.setTextDatum(top_left);
        sprite_.drawString(entry.text, kPad, y);

        y += kTextRowH;

        for (int16_t dx = kPad; dx < kScreenWidth - kPad; dx += kSepDotPitch) {
            sprite_.drawPixel(dx, y, kColorCardBorder);
        }
        y += kSepH;
    }

    sprite_.setTextColor(kColorTextDim);
    sprite_.setTextDatum(top_center);
    sprite_.drawString("tap to close", kScreenWidth / 2, y + 2);
}

}  // namespace mesh::ui
