#pragma once

#include <M5GFX.h>
#include "protocol/MeshTypes.h"

namespace mesh::ui {

class ToastManager;

struct RenderState {
    // Status bar
    const char* channelName = nullptr;
    protocol::NodeId nodeId = 0;
    uint8_t batteryPercent = 0;

    // Message card
    const char* messageText = nullptr;
    uint8_t messageIndex = 0;
    uint8_t messageCount = 0;

    // Hold-to-send feedback
    float holdProgress = 0.0f;
    bool isHolding = false;

    // Send feedback
    bool sentFlash = false;
    bool errorFlash = false;

    // Overlays
    bool showHistory = false;
    bool showHint = true;

    // Toast data
    const ToastManager* toastManager = nullptr;
    uint32_t nowMs = 0;
};

class Renderer {
public:
    void init();
    void render(const RenderState& state);

private:
    LGFX_Sprite sprite_;

    void drawStatusBar(const RenderState& state, int16_t y);
    void drawToast(const RenderState& state, int16_t y, int16_t& bottomY);
    void drawMessageCard(const RenderState& state, int16_t top, int16_t bottom);
    void drawPageDots(int16_t cx, int16_t y, uint8_t count, uint8_t active);
    void drawCenteredText(const char* text, int16_t cx, int16_t cy,
                          int16_t maxWidth, int16_t maxHeight, uint16_t color);
    void drawTruncated(const char* text, int16_t x, int16_t y,
                       int16_t maxWidth, uint16_t color);
    void drawHistory(const RenderState& state, int16_t top);
};

}  // namespace mesh::ui
