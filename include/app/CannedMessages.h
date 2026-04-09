#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>

namespace mesh::app {

class CannedMessages {
public:
    /// Initialize with message array and load persisted index.
    void init(const char* const* msgs, size_t count);

    /// Get the current message text.
    std::string_view current() const;

    /// Advance to the next message (wraps around). Persists index.
    void next();

    /// Go back to the previous message (wraps around).
    void previous();

    /// Number of messages.
    size_t count() const { return count_; }

    /// Current index.
    uint8_t index() const { return index_; }

    /// Persist current index to flash.
    void save();

private:
    const char* const* messages_ = nullptr;
    size_t count_ = 0;
    uint8_t index_ = 0;
};

}  // namespace mesh::app
