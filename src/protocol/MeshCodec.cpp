#include "protocol/MeshCodec.h"

#include <mbedtls/base64.h>
#include <cstring>

namespace mesh::protocol {

// ---- helpers (file-local) ----

static uint8_t xorHash(const uint8_t* bytes, size_t len) {
    uint8_t h = 0;
    for (size_t i = 0; i < len; ++i) {
        h ^= bytes[i];
    }
    return h;
}

static bool pbReadVarint(const uint8_t*& cursor, const uint8_t* end, uint64_t& val) {
    val = 0;
    uint8_t shift = 0;
    while (cursor < end && shift <= 63) {
        const uint8_t b = *cursor++;
        val |= static_cast<uint64_t>(b & 0x7F) << shift;
        if (!(b & 0x80)) return true;
        shift += 7;
    }
    return false;
}

static bool pbSkipField(const uint8_t*& cursor, const uint8_t* end, uint8_t wireType) {
    uint64_t tmp = 0;
    switch (wireType) {
        case 0: return pbReadVarint(cursor, end, tmp);
        case 1:
            if (static_cast<size_t>(end - cursor) < 8) return false;
            cursor += 8;
            return true;
        case 2:
            if (!pbReadVarint(cursor, end, tmp)) return false;
            if (static_cast<uint64_t>(end - cursor) < tmp) return false;
            cursor += static_cast<size_t>(tmp);
            return true;
        case 5:
            if (static_cast<size_t>(end - cursor) < 4) return false;
            cursor += 4;
            return true;
        default: return false;
    }
}

// ---- public API ----

bool decodeBase64Key(const char* base64, uint8_t outKey[kKeyLen]) {
    size_t decodedLen = 0;
    const int rc = mbedtls_base64_decode(
        outKey, kKeyLen, &decodedLen,
        reinterpret_cast<const unsigned char*>(base64),
        std::strlen(base64));
    return rc == 0 && decodedLen == kKeyLen;
}

uint8_t computeChannelHash(const char* channelName, const uint8_t key[kKeyLen]) {
    const uint8_t nameHash = xorHash(
        reinterpret_cast<const uint8_t*>(channelName), std::strlen(channelName));
    const uint8_t keyHash = xorHash(key, kKeyLen);
    return static_cast<uint8_t>(nameHash ^ keyHash);
}

NodeId nodeIdFromMac(const uint8_t mac[6]) {
    return (static_cast<uint32_t>(mac[2]) << 24)
         | (static_cast<uint32_t>(mac[3]) << 16)
         | (static_cast<uint32_t>(mac[4]) << 8)
         | (static_cast<uint32_t>(mac[5]));
}

size_t encodeTextPayload(const uint8_t* text, size_t textLen,
                         uint8_t* outBuf, size_t outBufMax) {
    if (textLen == 0 || textLen > kMaxTextLen) return 0;

    // Protobuf: field1(varint portnum) + field2(length-delimited payload)
    const size_t needed = 2 + 2 + textLen;
    if (outBufMax < needed) return 0;

    size_t pos = 0;
    outBuf[pos++] = 0x08;  // field 1, wire type 0 (varint)
    outBuf[pos++] = static_cast<uint8_t>(PortNum::TextMessage);
    outBuf[pos++] = 0x12;  // field 2, wire type 2 (length-delimited)
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

    while (cursor < end) {
        uint64_t key = 0;
        if (!pbReadVarint(cursor, end, key)) return false;

        const uint32_t field = static_cast<uint32_t>(key >> 3);
        const uint8_t wire = static_cast<uint8_t>(key & 0x07);

        if (field == 1 && wire == 0) {
            uint64_t v = 0;
            if (!pbReadVarint(cursor, end, v)) return false;
            portOut = static_cast<uint32_t>(v);
            continue;
        }
        if (field == 2 && wire == 2) {
            uint64_t pLen = 0;
            if (!pbReadVarint(cursor, end, pLen)) return false;
            if (static_cast<uint64_t>(end - cursor) < pLen) return false;
            payloadOut = cursor;
            payloadLenOut = static_cast<size_t>(pLen);
            cursor += payloadLenOut;
            continue;
        }
        if (!pbSkipField(cursor, end, wire)) return false;
    }
    return true;
}

}  // namespace mesh::protocol
