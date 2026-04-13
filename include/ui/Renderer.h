#pragma once

/// Full-frame UI renderer using LGFX double-buffering.
///
/// Rendering strategy: the entire 135×240 screen is drawn into an off-screen
/// LGFX_Sprite each frame, then pushed to the display in one DMA transfer
/// via pushSprite(). This eliminates flicker and tearing — no partial updates.
///
/// The renderer is stateless: it reads a RenderState snapshot each frame and
/// draws the entire UI from scratch. The "dirty flag" pattern in main.cpp
/// ensures render() is only called when something actually changed.
///
/// Screen layout (top → bottom):
///   1. Status bar — node ID + battery level
///   2. Toast notification (optional) — incoming message with countdown timer
///   3. Message card — current canned message with word-wrapping, page dots,
///      hold-to-send progress bar, and sent/error flash feedback
///   4. History overlay (optional) — full-screen list of recent received messages

#include <M5GFX.h>
#include "protocol/MeshTypes.h"

namespace mesh::ui {

class ToastManager;

/// Snapshot of all data needed to render one frame. Populated by main.cpp
/// each time the dirty flag is set. All pointers must remain valid until
/// render() returns (they point to static/module-level data).
struct RenderState {
    // ── Status bar ──
    const char* channelName = nullptr;   ///< Meshtastic channel name (from secrets.h)
    protocol::NodeId nodeId = 0;         ///< Local node ID (displayed as !XXXXXXXX)
    uint8_t batteryPercent = 0;          ///< Battery level 0–100 from PMIC
    bool isCharging = false;             ///< True when USB power is connected
    bool stayAwake = false;              ///< Show stay-awake indicator in status bar

    // ── Message card ──
    const char* messageText = nullptr;   ///< Current canned message text
    uint8_t messageIndex = 0;            ///< Currently selected message index
    uint8_t messageCount = 0;            ///< Total number of canned messages

    // ── Hold-to-send feedback ──
    float holdProgress = 0.0f;           ///< 0.0–1.0 progress of touch hold
    bool isHolding = false;              ///< True while finger is held down

    // ── Send feedback ──
    bool sentFlash = false;              ///< True during post-send teal flash
    bool errorFlash = false;             ///< True during post-error red flash

    // ── Overlays ──
    bool showHistory = false;            ///< True when history overlay is visible
    bool showHint = true;                ///< True until first user interaction (shows "swipe | hold")

    // ── Toast data ──
    const ToastManager* toastManager = nullptr;  ///< Toast/history data source
    uint32_t nowMs = 0;                  ///< Current millis() for toast countdown
};

class Renderer {
public:
    /// Create the off-screen sprite (135×240, 16-bit color, internal SRAM).
    void init();

    /// Draw the full UI into the sprite and push to the display.
    void render(const RenderState& state);

private:
    LGFX_Sprite sprite_;  ///< Off-screen buffer for flicker-free rendering

    /// Draw the top status bar: node ID (left), battery icon + percentage (right).
    void drawStatusBar(const RenderState& state, int16_t y);

    /// Draw the toast notification below the status bar (if active).
    /// Updates bottomY to reflect how much vertical space the toast consumed.
    void drawToast(const RenderState& state, int16_t y, int16_t& bottomY);

    /// Draw the message card filling the space between top and bottom.
    /// Includes: hint text, channel name, wrapped message text, page dots,
    /// hold progress bar, and sent/error flash background.
    void drawMessageCard(const RenderState& state, int16_t top, int16_t bottom);

    /// Draw horizontal pagination dots (one per canned message).
    void drawPageDots(int16_t cx, int16_t y, uint8_t count, uint8_t active);

    /// Draw text centered at (cx, cy) with automatic word-wrapping and font
    /// size reduction. Falls back from size 2 → 1.5 → 1 if the text is too
    /// wide. Wraps to multiple lines within maxWidth × maxHeight, truncating
    /// the last line with ".." if text overflows.
    void drawCenteredText(const char* text, int16_t cx, int16_t cy,
                          int16_t maxWidth, int16_t maxHeight, uint16_t color);

    /// Draw single-line text at (x, y), truncating with ".." if wider than maxWidth.
    /// Used for toast message text and history entry text.
    void drawTruncated(const char* text, int16_t x, int16_t y,
                       int16_t maxWidth, uint16_t color);

    /// Draw the full-screen message history overlay (swipe-down to open).
    void drawHistory(const RenderState& state, int16_t top);
};

}  // namespace mesh::ui
