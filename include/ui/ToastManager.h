#pragma once

#include <cstdint>
#include <cstddef>
#include "config/UIConfig.h"
#include "protocol/MeshTypes.h"

namespace mesh::ui {

struct HistoryEntry {
    protocol::NodeId sender = 0;
    char text[config::ui::kHistoryTextMaxLen] = {};
    uint32_t timestampMs = 0;
    bool valid = false;
};

class ToastManager {
public:
    void addMessage(protocol::NodeId sender, const char* text, uint32_t nowMs);
    bool update(uint32_t nowMs);

    bool hasActiveToast() const { return toastActive_; }
    const HistoryEntry& activeToast() const;
    float toastProgress(uint32_t nowMs) const;

    size_t historyCount() const { return historyCount_; }
    const HistoryEntry& historyAt(size_t index) const;

private:
    HistoryEntry history_[config::ui::kHistoryMaxEntries] = {};
    size_t historyCount_ = 0;
    size_t historyHead_ = 0;

    bool toastActive_ = false;
    uint32_t toastStartMs_ = 0;
    size_t toastEntryIndex_ = 0;
};

}  // namespace mesh::ui
