#pragma once

/// Core types and constants for the Meshtastic mesh protocol.
///
/// Meshtastic over-the-air frame layout (sent/received via LoRa):
///
///   Byte offset   Field            Size    Encoding
///   ──────────────────────────────────────────────────
///   0..3          dest             4 B     LE uint32 — destination node ID (0xFFFFFFFF = broadcast)
///   4..7          source           4 B     LE uint32 — sender node ID
///   8..11         packetId         4 B     LE uint32 — unique packet identifier
///   12            flags            1 B     bitfield (see makeMeshFlags)
///   13            channelHash      1 B     XOR-based channel identifier
///   14            nextHop          1 B     (unused in this implementation)
///   15            relayNode        1 B     (unused in this implementation)
///   16..N         encrypted payload        AES-256-CTR encrypted protobuf Data message
///
/// The encrypted payload, once decrypted, is a protobuf-encoded "Data" message:
///   field 1 (varint)            — portnum (1 = TextMessage, 4 = Routing, etc.)
///   field 2 (length-delimited)  — application payload (e.g. UTF-8 text for TextMessage)
///
/// Encryption uses AES-256-CTR with a 16-byte nonce:
///   nonce = packetId (8 bytes LE) + sourceNode (4 bytes LE) + zeros (4 bytes)

#include <cstdint>
#include <cstddef>

namespace mesh::protocol {

using NodeId   = uint32_t;   ///< Meshtastic node identifier (derived from MAC address)
using PacketId = uint32_t;   ///< Unique per-packet identifier (random start, then incrementing)

inline constexpr NodeId kBroadcastAddr     = 0xFFFFFFFF;  ///< Send to all nodes on the channel
inline constexpr size_t kMeshHeaderLen     = 16;          ///< Fixed header size before encrypted payload
inline constexpr size_t kMaxEncryptedLen   = 240;         ///< Max payload after header (LoRa frame limit)
inline constexpr size_t kMaxTextLen        = 127;         ///< Max text bytes in a single message
inline constexpr size_t kRxBufferLen       = 255;         ///< RadioLib max receive buffer
inline constexpr size_t kKeyLen            = 32;          ///< AES-256 key size in bytes
inline constexpr size_t kKeyBits           = 256;         ///< AES-256 key size in bits (for mbedtls)
inline constexpr size_t kNonceLen          = 16;          ///< AES-CTR nonce/IV size
inline constexpr size_t kMinProtoPayloadLen = 2;          ///< Smallest valid protobuf (1-byte tag + 1-byte value)

// ── Mesh header field byte offsets ─────────────────────────────────────────
// See frame layout diagram above. All multi-byte fields are little-endian.

inline constexpr size_t kOffDest        = 0;   ///< Destination node ID (4 bytes LE)
inline constexpr size_t kOffSource      = 4;   ///< Source node ID (4 bytes LE)
inline constexpr size_t kOffPacketId    = 8;   ///< Packet identifier (4 bytes LE)
inline constexpr size_t kOffFlags       = 12;  ///< Flags bitfield (1 byte)
inline constexpr size_t kOffChannelHash = 13;  ///< Channel hash (1 byte)
inline constexpr size_t kOffNextHop     = 14;  ///< Next-hop node (1 byte, unused)
inline constexpr size_t kOffRelayNode   = 15;  ///< Relay node (1 byte, unused)

// ── AES-CTR nonce layout ───────────────────────────────────────────────────
// The 16-byte nonce is: packetId (8 bytes LE) + sourceNode (4 bytes LE) + zeros (4 bytes)

inline constexpr size_t kNoncePacketIdLen   = 8;  ///< Bytes occupied by packetId in nonce (LE, upper 4 zero)
inline constexpr size_t kNonceSourceOffset  = 8;  ///< Byte offset of sourceNode within the nonce
inline constexpr size_t kNonceSourceLen     = 4;  ///< Bytes occupied by sourceNode in nonce

// ── MAC-to-NodeId mapping ──────────────────────────────────────────────────
// Meshtastic ESP32 convention: node ID = last 4 bytes of 6-byte MAC (big-endian).

inline constexpr size_t kMacLen             = 6;  ///< IEEE 802 MAC address length
inline constexpr size_t kMacNodeIdOffset    = 2;  ///< First MAC byte used for node ID

/// Meshtastic application port numbers.
/// Only TextMessage is used by this device; other port numbers (Routing, Position, etc.)
/// are received but silently ignored during parsing.
enum class PortNum : uint8_t {
    TextMessage = 1,   ///< UTF-8 text payload
};

// ── Protobuf Data message field tags ───────────────────────────────────────
// Meshtastic Data message fields we encode/decode. Tags are (fieldNumber << 3 | wireType).

inline constexpr uint8_t kProtoTagPortnum  = 0x08;  ///< Field 1, wire type 0 (varint) — portnum
inline constexpr uint8_t kProtoTagPayload  = 0x12;  ///< Field 2, wire type 2 (length-delimited) — payload
inline constexpr uint8_t kProtoFieldPortnum = 1;     ///< Protobuf field number for portnum
inline constexpr uint8_t kProtoFieldPayload = 2;     ///< Protobuf field number for payload

/// Error codes returned by protocol and radio operations.
enum class MeshError : uint8_t {
    Ok,
    InvalidArg,
    KeyDecodeFailed,
    RadioInitFailed,
    RadioTxFailed,
    RadioRxFailed,
    PacketTooShort,     ///< Frame shorter than 16-byte header + minimum payload
    PacketTooLong,      ///< Encrypted payload exceeds kMaxEncryptedLen
    EncryptionFailed,
    DecryptionFailed,
    EncodeFailed,
    DecodeFailed,       ///< Protobuf parse error (corrupt or unknown format)
    BufferTooSmall,
};

/// Parsed mesh frame header (first 16 bytes of the over-the-air frame).
struct PacketHeader {
    NodeId   dest       = kBroadcastAddr;  ///< Destination node (0xFFFFFFFF = broadcast)
    NodeId   source     = 0;               ///< Sender's node ID
    PacketId packetId   = 0;               ///< Unique packet ID (also used as encryption nonce)
    uint8_t  flags      = 0;               ///< Bitfield: hopLimit[2:0] | wantAck[3] | viaMqtt[4] | hopStart[7:5]
    uint8_t  channelHash = 0;              ///< Channel identifier (XOR of channel name and key hashes)
    uint8_t  nextHop    = 0;               ///< Next-hop routing (unused, set to 0)
    uint8_t  relayNode  = 0;               ///< Relay node (unused, set to 0)
};

/// Write a 32-bit value in little-endian byte order.
inline void putLe32(uint8_t* dst, uint32_t value) {
    dst[0] = static_cast<uint8_t>(value);
    dst[1] = static_cast<uint8_t>(value >> 8);
    dst[2] = static_cast<uint8_t>(value >> 16);
    dst[3] = static_cast<uint8_t>(value >> 24);
}

/// Read a 32-bit value from little-endian byte order.
inline uint32_t getLe32(const uint8_t* src) {
    return static_cast<uint32_t>(src[0])
         | (static_cast<uint32_t>(src[1]) << 8)
         | (static_cast<uint32_t>(src[2]) << 16)
         | (static_cast<uint32_t>(src[3]) << 24);
}

/// Pack Meshtastic mesh flags into a single byte.
///
/// Bit layout:
///   [2:0]  hopLimit  — remaining hop count (0-7, typically 3)
///   [3]    wantAck   — request acknowledgment (only works for unicast, not broadcast)
///   [4]    viaMqtt   — packet was bridged from MQTT
///   [7:5]  hopStart  — original hop count when first transmitted
inline uint8_t makeMeshFlags(uint8_t hopLimit, bool wantAck, bool viaMqtt, uint8_t hopStart) {
    return static_cast<uint8_t>(
        (hopLimit & 0x07)
        | (wantAck ? 0x08 : 0x00)
        | (viaMqtt ? 0x10 : 0x00)
        | ((hopStart & 0x07) << 5)
    );
}

}  // namespace mesh::protocol
