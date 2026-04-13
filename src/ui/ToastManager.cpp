#include "ui/ToastManager.h"

#include <cstring>
#include <algorithm>

namespace mesh::ui {

/// Add a message to the history ring buffer and display it as a toast.
///
/// The message is written to history_[historyHead_], which is then advanced.
/// If the buffer is full (historyCount_ == kHistoryMaxEntries), the oldest
/// entry is silently overwritten — no reallocation, O(1) insertion.
///
/// The new entry also becomes the active toast, replacing any existing one.
/// There's no toast queue — only the latest message is shown.
void ToastManager::addMessage(protocol::NodeId sender, const char* text, uint32_t nowMs,
                              int8_t snr, uint8_t hops) {
    auto& entry = history_[historyHead_];
    entry.sender = sender;
    entry.timestampMs = nowMs;
    entry.snr = snr;
    entry.hops = hops;
    entry.valid = true;
    strncpy(entry.text, text, config::ui::kHistoryTextMaxLen - 1);
    entry.text[config::ui::kHistoryTextMaxLen - 1] = '\0';  // Ensure null-termination

    // Activate this entry as the current toast notification
    toastEntryIndex_ = historyHead_;
    toastActive_ = true;
    toastStartMs_ = nowMs;

    // Advance ring buffer head (wraps around)
    historyHead_ = (historyHead_ + 1) % config::ui::kHistoryMaxEntries;
    if (historyCount_ < config::ui::kHistoryMaxEntries) {
        ++historyCount_;
    }
}

/// Check if the active toast has expired. Called every loop iteration.
/// Returns true if a toast is visible (signals the caller to set the dirty
/// flag, since the countdown bar needs to animate each frame).
bool ToastManager::update(uint32_t nowMs) {
    if (!toastActive_) return false;

    if (nowMs - toastStartMs_ >= config::ui::kToastDurationMs) {
        toastActive_ = false;  // Toast expired — renderer will stop drawing it
    }
    return true;  // Redraw every frame while toast is visible (countdown bar animates)
}

/// Return the history entry backing the current toast notification.
const HistoryEntry& ToastManager::activeToast() const {
    return history_[toastEntryIndex_];
}

/// Compute how far through its display duration the toast is.
/// Returns 0.0 when the toast just appeared, 1.0 when it should disappear.
/// Used by the renderer to draw the shrinking countdown bar.
float ToastManager::toastProgress(uint32_t nowMs) const {
    if (!toastActive_) return 1.0f;
    const uint32_t elapsed = nowMs - toastStartMs_;
    return std::min(1.0f, static_cast<float>(elapsed) / config::ui::kToastDurationMs);
}

/// Access a history entry by recency index (0 = newest, 1 = second newest, ...).
/// Maps the logical index to the physical ring buffer position by walking
/// backwards from historyHead_.
/// Returns a static empty entry for out-of-range indices (safe to dereference).
const HistoryEntry& ToastManager::historyAt(size_t index) const {
    const size_t actualCount = historyCount_;
    if (index >= actualCount) {
        static const HistoryEntry empty{};
        return empty;
    }
    // Walk backwards from head: head-1 is the most recent entry
    const size_t pos = (historyHead_ + config::ui::kHistoryMaxEntries - 1 - index)
                       % config::ui::kHistoryMaxEntries;
    return history_[pos];
}

}  // namespace mesh::ui
