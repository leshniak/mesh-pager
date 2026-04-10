#pragma once

/// Toast notification and message history manager.
///
/// Serves two purposes:
///   1. **Active toast**: Shows a temporary notification banner when a message
///      is received (or sent). The toast auto-dismisses after kToastDurationMs.
///      Only one toast is active at a time — new messages replace the current one.
///
///   2. **Message history**: Maintains a fixed-size circular buffer of the most
///      recent messages. The history overlay (swipe-down) reads from this buffer.
///      When the buffer is full, the oldest entry is overwritten.
///
/// Both TX and RX messages go through addMessage(), so the history shows a
/// unified timeline of all traffic on the channel.

#include <cstdint>
#include <cstddef>
#include "config/UIConfig.h"
#include "protocol/MeshTypes.h"

namespace mesh::ui {

/// A single entry in the message history ring buffer.
struct HistoryEntry {
    protocol::NodeId sender = 0;                        ///< Sender node ID (ours for TX)
    char text[config::ui::kHistoryTextMaxLen] = {};      ///< Message text (truncated to fit)
    uint32_t timestampMs = 0;                            ///< millis() when the message was added
    int8_t snr = 0;                                      ///< SNR in dB (0 = not available, e.g. TX)
    bool valid = false;                                  ///< True if this slot has been written
};

class ToastManager {
public:
    /// Add a message to the history and activate it as the current toast.
    /// If a toast is already active, it is replaced (no queue).
    void addMessage(protocol::NodeId sender, const char* text, uint32_t nowMs,
                    int8_t snr = 0);

    /// Check if the active toast has expired. Returns true if a toast is visible
    /// (caller should set dirty flag to keep animating the countdown bar).
    bool update(uint32_t nowMs);

    /// True if a toast is currently being displayed.
    bool hasActiveToast() const { return toastActive_; }

    /// Get the currently displayed toast entry (only valid if hasActiveToast()).
    const HistoryEntry& activeToast() const;

    /// Compute toast countdown progress: 0.0 (just appeared) → 1.0 (expired).
    float toastProgress(uint32_t nowMs) const;

    /// Number of valid entries in the history (0 to kHistoryMaxEntries).
    size_t historyCount() const { return historyCount_; }

    /// Get a history entry by index. Index 0 = most recent, higher = older.
    /// Returns a static empty entry if index is out of range.
    const HistoryEntry& historyAt(size_t index) const;

private:
    /// Circular buffer of history entries. historyHead_ points to the next
    /// slot to write (wraps around). Entries are ordered newest-first when
    /// accessed via historyAt().
    HistoryEntry history_[config::ui::kHistoryMaxEntries] = {};
    size_t historyCount_ = 0;  ///< Number of valid entries (capped at kHistoryMaxEntries)
    size_t historyHead_ = 0;   ///< Next write position in the ring buffer

    bool toastActive_ = false;       ///< True while a toast notification is visible
    uint32_t toastStartMs_ = 0;      ///< millis() when the current toast was created
    size_t toastEntryIndex_ = 0;     ///< Index into history_[] for the active toast
};

}  // namespace mesh::ui
