#pragma once

/// Pre-configured canned message manager.
///
/// Stores a pointer to the compile-time message array (defined in secrets.h)
/// and tracks the currently selected index. The index is persisted to NVS flash
/// so the device remembers which message was selected across deep sleep cycles.
///
/// Messages are navigated via next()/previous() (wrapping at both ends).
/// The save() method writes the current index to flash — called before sleep
/// to avoid unnecessary flash writes on every navigation.

#include <cstdint>
#include <cstddef>
#include <string_view>

namespace mesh::app {

class CannedMessages {
public:
    /// Initialize with message array and load persisted index from NVS flash.
    void init(const char* const* msgs, size_t count);

    /// Get the current message text.
    std::string_view current() const;

    /// Advance to the next message (wraps around).
    void next();

    /// Go back to the previous message (wraps around).
    void previous();

    /// Number of messages.
    size_t count() const { return count_; }

    /// Current index.
    uint8_t index() const { return index_; }

    /// Persist current index to NVS flash. Only writes if the index changed.
    void save();

private:
    const char* const* messages_ = nullptr;
    size_t count_ = 0;
    uint8_t index_ = 0;
};

}  // namespace mesh::app
