#pragma once

/// Packet deduplication cache.
///
/// Meshtastic mesh networks relay packets through multiple nodes, so the same
/// packet can arrive several times via different paths. This cache remembers
/// recently seen (source, packetId) pairs and rejects duplicates.
///
/// Implementation mirrors the Meshtastic firmware: a fixed-size flat array
/// with linear scan and timestamp-based expiry.

#include <cstdint>
#include <cstddef>

namespace mesh::protocol {

template <size_t Capacity = 64, uint32_t ExpiryMs = 600000>
class PacketDedup {
public:
    /// Returns true if this packet was already seen (duplicate).
    /// If new, records it and returns false.
    bool isDuplicate(uint32_t source, uint32_t packetId, uint32_t nowMs) {
        evictExpired(nowMs);

        for (size_t i = 0; i < size_; ++i) {
            if (entries_[i].source == source && entries_[i].packetId == packetId) {
                return true;
            }
        }

        insert(source, packetId, nowMs);
        return false;
    }

private:
    struct Entry {
        uint32_t source;
        uint32_t packetId;
        uint32_t timestampMs;
    };

    Entry entries_[Capacity] = {};
    size_t size_ = 0;

    void evictExpired(uint32_t nowMs) {
        size_t write = 0;
        for (size_t read = 0; read < size_; ++read) {
            if (nowMs - entries_[read].timestampMs < ExpiryMs) {
                entries_[write++] = entries_[read];
            }
        }
        size_ = write;
    }

    void insert(uint32_t source, uint32_t packetId, uint32_t nowMs) {
        if (size_ < Capacity) {
            entries_[size_++] = {source, packetId, nowMs};
        } else {
            // Overwrite oldest entry
            size_t oldest = 0;
            for (size_t i = 1; i < Capacity; ++i) {
                if (entries_[i].timestampMs < entries_[oldest].timestampMs) {
                    oldest = i;
                }
            }
            entries_[oldest] = {source, packetId, nowMs};
        }
    }
};

}  // namespace mesh::protocol
