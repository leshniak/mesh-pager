#pragma once

#include <cstdint>
#include <cstddef>

namespace mesh::protocol {

using NodeId   = uint32_t;
using PacketId = uint32_t;

inline constexpr NodeId kBroadcastAddr     = 0xFFFFFFFF;
inline constexpr size_t kMeshHeaderLen     = 16;
inline constexpr size_t kMaxEncryptedLen   = 240;
inline constexpr size_t kMaxTextLen        = 127;
inline constexpr size_t kRxBufferLen       = 255;
inline constexpr size_t kKeyLen            = 32;
inline constexpr size_t kNonceLen          = 16;

enum class PortNum : uint8_t {
    TextMessage = 1,
};

enum class MeshError : uint8_t {
    Ok,
    InvalidArg,
    KeyDecodeFailed,
    RadioInitFailed,
    RadioTxFailed,
    RadioRxFailed,
    PacketTooShort,
    PacketTooLong,
    EncryptionFailed,
    DecryptionFailed,
    EncodeFailed,
    DecodeFailed,
    BufferTooSmall,
};

struct PacketHeader {
    NodeId   dest       = kBroadcastAddr;
    NodeId   source     = 0;
    PacketId packetId   = 0;
    uint8_t  flags      = 0;
    uint8_t  channelHash = 0;
    uint8_t  nextHop    = 0;
    uint8_t  relayNode  = 0;
};

// Little-endian helpers
inline void putLe32(uint8_t* dst, uint32_t value) {
    dst[0] = static_cast<uint8_t>(value);
    dst[1] = static_cast<uint8_t>(value >> 8);
    dst[2] = static_cast<uint8_t>(value >> 16);
    dst[3] = static_cast<uint8_t>(value >> 24);
}

inline uint32_t getLe32(const uint8_t* src) {
    return static_cast<uint32_t>(src[0])
         | (static_cast<uint32_t>(src[1]) << 8)
         | (static_cast<uint32_t>(src[2]) << 16)
         | (static_cast<uint32_t>(src[3]) << 24);
}

inline uint8_t makeMeshFlags(uint8_t hopLimit, bool wantAck, bool viaMqtt, uint8_t hopStart) {
    return static_cast<uint8_t>(
        (hopLimit & 0x07)
        | (wantAck ? 0x08 : 0x00)
        | (viaMqtt ? 0x10 : 0x00)
        | ((hopStart & 0x07) << 5)
    );
}

}  // namespace mesh::protocol
