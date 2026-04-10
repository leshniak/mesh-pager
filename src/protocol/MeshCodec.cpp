#include "protocol/MeshCodec.h"

#include <mbedtls/base64.h>
#include <cstring>

namespace mesh::protocol {

// ============================================================================
// Helpers (file-local)
// ============================================================================

/// XOR all bytes together — used for channel hash computation.
static uint8_t xorHash(const uint8_t* bytes, size_t len) {
    uint8_t h = 0;
    for (size_t i = 0; i < len; ++i) {
        h ^= bytes[i];
    }
    return h;
}

/// Read a protobuf base-128 varint from the cursor.
/// Varints encode unsigned integers in 7-bit groups with continuation bits.
/// Returns false if the buffer runs out or the varint exceeds 64 bits.
static bool pbReadVarint(const uint8_t*& cursor, const uint8_t* end, uint64_t& val) {
    val = 0;
    uint8_t shift = 0;
    while (cursor < end && shift <= 63) {
        const uint8_t b = *cursor++;
        val |= static_cast<uint64_t>(b & 0x7F) << shift;
        if (!(b & 0x80)) return true;  // no continuation bit — done
        shift += 7;
    }
    return false;
}

/// Skip over a protobuf field we don't care about.
/// Wire types: 0=varint, 1=fixed64, 2=length-delimited, 5=fixed32.
/// This allows forward-compatibility — unknown fields from newer Meshtastic
/// versions are silently skipped instead of causing parse failures.
static bool pbSkipField(const uint8_t*& cursor, const uint8_t* end, uint8_t wireType) {
    uint64_t tmp = 0;
    switch (wireType) {
        case 0: return pbReadVarint(cursor, end, tmp);          // varint
        case 1:                                                  // fixed 64-bit
            if (static_cast<size_t>(end - cursor) < 8) return false;
            cursor += 8;
            return true;
        case 2:                                                  // length-delimited
            if (!pbReadVarint(cursor, end, tmp)) return false;
            if (static_cast<uint64_t>(end - cursor) < tmp) return false;
            cursor += static_cast<size_t>(tmp);
            return true;
        case 5:                                                  // fixed 32-bit
            if (static_cast<size_t>(end - cursor) < 4) return false;
            cursor += 4;
            return true;
        default: return false;
    }
}

// ============================================================================
// Public API
// ============================================================================

bool decodeBase64Key(const char* base64, uint8_t outKey[kKeyLen]) {
    size_t decodedLen = 0;
    const int rc = mbedtls_base64_decode(
        outKey, kKeyLen, &decodedLen,
        reinterpret_cast<const unsigned char*>(base64),
        std::strlen(base64));
    return rc == 0 && decodedLen == kKeyLen;
}

uint8_t computeChannelHash(const char* channelName, const uint8_t key[kKeyLen]) {
    // Meshtastic channel hash: XOR of all channel name bytes XOR all key bytes.
    // This produces a single byte that receivers use to quickly filter packets
    // before attempting the more expensive AES decryption.
    const uint8_t nameHash = xorHash(
        reinterpret_cast<const uint8_t*>(channelName), std::strlen(channelName));
    const uint8_t keyHash = xorHash(key, kKeyLen);
    return static_cast<uint8_t>(nameHash ^ keyHash);
}

NodeId nodeIdFromMac(const uint8_t mac[kMacLen]) {
    // Meshtastic ESP32 convention: node ID = last 4 bytes of MAC in big-endian.
    // For a MAC of AA:BB:CC:DD:EE:FF, node ID = 0xCCDDEEFF.
    const size_t o = kMacNodeIdOffset;
    return (static_cast<uint32_t>(mac[o])     << 24)
         | (static_cast<uint32_t>(mac[o + 1]) << 16)
         | (static_cast<uint32_t>(mac[o + 2]) << 8)
         | (static_cast<uint32_t>(mac[o + 3]));
}

size_t encodeTextPayload(const uint8_t* text, size_t textLen,
                         uint8_t* outBuf, size_t outBufMax) {
    if (textLen == 0 || textLen > kMaxTextLen) return 0;

    // Protobuf encoding of a Meshtastic Data message:
    //   field 1 (varint): portnum = TextMessage (1)
    //   field 2 (length-delimited): text payload bytes
    //
    // Byte layout:
    //   0x08 0x01         — field 1, wire type 0 (varint), value 1
    //   0x12 <len> <data> — field 2, wire type 2 (length-delimited)
    const size_t needed = 2 + 2 + textLen;
    if (outBufMax < needed) return 0;

    size_t pos = 0;
    outBuf[pos++] = kProtoTagPortnum;
    outBuf[pos++] = static_cast<uint8_t>(PortNum::TextMessage);
    outBuf[pos++] = kProtoTagPayload;
    outBuf[pos++] = static_cast<uint8_t>(textLen);
    std::memcpy(outBuf + pos, text, textLen);
    pos += textLen;
    return pos;
}

bool decodeDataPayload(const uint8_t* data, size_t dataLen,
                       uint32_t& portOut,
                       const uint8_t*& payloadOut, size_t& payloadLenOut) {
    portOut = 0;
    payloadOut = nullptr;
    payloadLenOut = 0;

    const uint8_t* cursor = data;
    const uint8_t* end = data + dataLen;

    // Walk through protobuf fields. We only extract field 1 (portnum) and
    // field 2 (payload). All other fields (request_id, reply_id, emoji, etc.)
    // are skipped via pbSkipField for forward-compatibility.
    while (cursor < end) {
        uint64_t key = 0;
        if (!pbReadVarint(cursor, end, key)) return false;

        const uint32_t field = static_cast<uint32_t>(key >> 3);   // field number
        const uint8_t wire = static_cast<uint8_t>(key & 0x07);    // wire type

        if (field == kProtoFieldPortnum && wire == 0) {
            // portnum (varint) — identifies the application (TextMessage, Routing, etc.)
            uint64_t v = 0;
            if (!pbReadVarint(cursor, end, v)) return false;
            portOut = static_cast<uint32_t>(v);
            continue;
        }
        if (field == kProtoFieldPayload && wire == 2) {
            // payload (length-delimited) — the actual application data
            uint64_t pLen = 0;
            if (!pbReadVarint(cursor, end, pLen)) return false;
            if (static_cast<uint64_t>(end - cursor) < pLen) return false;
            payloadOut = cursor;
            payloadLenOut = static_cast<size_t>(pLen);
            cursor += payloadLenOut;
            continue;
        }
        // Unknown field — skip it to maintain forward-compatibility
        if (!pbSkipField(cursor, end, wire)) return false;
    }
    return true;
}

}  // namespace mesh::protocol
