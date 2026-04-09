#include "protocol/MeshPacket.h"
#include "protocol/MeshCrypto.h"
#include "protocol/MeshCodec.h"

#include <cstring>

namespace mesh::protocol {

size_t buildPacket(const PacketHeader& header,
                   uint8_t* plainPayload, size_t payloadLen,
                   const uint8_t key[kKeyLen],
                   uint8_t* outFrame, size_t outFrameMax) {
    const size_t totalLen = kMeshHeaderLen + payloadLen;
    if (outFrameMax < totalLen) return 0;

    // Encrypt the protobuf payload in-place before framing.
    // The nonce is derived from packetId + source, so each packet gets a unique keystream.
    aesCtrCrypt(plainPayload, payloadLen, header.packetId, header.source, key);

    // Write the 16-byte mesh header (all fields little-endian where multi-byte)
    putLe32(outFrame + 0, header.dest);
    putLe32(outFrame + 4, header.source);
    putLe32(outFrame + 8, header.packetId);
    outFrame[12] = header.flags;
    outFrame[13] = header.channelHash;
    outFrame[14] = header.nextHop;
    outFrame[15] = header.relayNode;

    // Append the now-encrypted payload after the header
    std::memcpy(outFrame + kMeshHeaderLen, plainPayload, payloadLen);
    return totalLen;
}

MeshError parsePacket(const uint8_t* frame, size_t frameLen,
                      const uint8_t key[kKeyLen],
                      PacketHeader& outHeader,
                      char* outText, size_t outTextMax, size_t& outTextLen) {
    outTextLen = 0;

    // Minimum frame: 16-byte header + at least 2 bytes of protobuf
    if (frameLen < kMeshHeaderLen + 2) return MeshError::PacketTooShort;

    // Extract the 16-byte header
    outHeader.dest       = getLe32(frame + 0);
    outHeader.source     = getLe32(frame + 4);
    outHeader.packetId   = getLe32(frame + 8);
    outHeader.flags      = frame[12];
    outHeader.channelHash = frame[13];
    outHeader.nextHop    = frame[14];
    outHeader.relayNode  = frame[15];

    // Decrypt the payload into a local buffer (don't modify the original frame)
    const size_t encLen = frameLen - kMeshHeaderLen;
    if (encLen > kMaxEncryptedLen) return MeshError::PacketTooLong;

    uint8_t decrypted[kMaxEncryptedLen];
    std::memcpy(decrypted, frame + kMeshHeaderLen, encLen);
    aesCtrCrypt(decrypted, encLen, outHeader.packetId, outHeader.source, key);

    // Decode the protobuf Data message to extract port number and payload
    uint32_t port = 0;
    const uint8_t* payload = nullptr;
    size_t payloadLen = 0;
    if (!decodeDataPayload(decrypted, encLen, port, payload, payloadLen)) {
        return MeshError::DecodeFailed;
    }

    // Only extract text from TextMessage port. Other ports (Routing, Position, etc.)
    // are valid packets but contain no text for us to display.
    if (port != static_cast<uint32_t>(PortNum::TextMessage) || !payload || payloadLen == 0) {
        return MeshError::Ok;
    }

    // Sanitize to printable ASCII — replace control chars and high bytes with '.'
    const size_t copyLen = (payloadLen < outTextMax - 1) ? payloadLen : (outTextMax - 1);
    for (size_t i = 0; i < copyLen; ++i) {
        const uint8_t ch = payload[i];
        outText[i] = (ch >= 32 && ch <= 126) ? static_cast<char>(ch) : '.';
    }
    outText[copyLen] = '\0';
    outTextLen = copyLen;

    return MeshError::Ok;
}

}  // namespace mesh::protocol
