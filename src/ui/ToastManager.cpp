#include "ui/ToastManager.h"

#include <cstring>
#include <algorithm>

namespace mesh::ui {

void ToastManager::addMessage(protocol::NodeId sender, const char* text, uint32_t nowMs) {
    auto& entry = history_[historyHead_];
    entry.sender = sender;
    entry.timestampMs = nowMs;
    entry.valid = true;
    strncpy(entry.text, text, config::ui::kHistoryTextMaxLen - 1);
    entry.text[config::ui::kHistoryTextMaxLen - 1] = '\0';

    toastEntryIndex_ = historyHead_;
    toastActive_ = true;
    toastStartMs_ = nowMs;

    historyHead_ = (historyHead_ + 1) % config::ui::kHistoryMaxEntries;
    if (historyCount_ < config::ui::kHistoryMaxEntries) {
        ++historyCount_;
    }
}

bool ToastManager::update(uint32_t nowMs) {
    if (!toastActive_) return false;

    if (nowMs - toastStartMs_ >= config::ui::kToastDurationMs) {
        toastActive_ = false;
        return true;
    }
    return false;
}

const HistoryEntry& ToastManager::activeToast() const {
    return history_[toastEntryIndex_];
}

float ToastManager::toastProgress(uint32_t nowMs) const {
    if (!toastActive_) return 1.0f;
    const uint32_t elapsed = nowMs - toastStartMs_;
    return std::min(1.0f, static_cast<float>(elapsed) / config::ui::kToastDurationMs);
}

const HistoryEntry& ToastManager::historyAt(size_t index) const {
    const size_t actualCount = historyCount_;
    if (index >= actualCount) {
        static const HistoryEntry empty{};
        return empty;
    }
    const size_t pos = (historyHead_ + config::ui::kHistoryMaxEntries - 1 - index)
                       % config::ui::kHistoryMaxEntries;
    return history_[pos];
}

}  // namespace mesh::ui
