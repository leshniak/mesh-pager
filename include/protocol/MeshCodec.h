#pragma once

#include "protocol/MeshTypes.h"

namespace mesh::protocol {

/// Decode a base64-encoded 32-byte key into outKey.
bool decodeBase64Key(const char* base64, uint8_t outKey[kKeyLen]);

/// Compute the Meshtastic channel hash byte from channel name and key.
uint8_t computeChannelHash(const char* channelName, const uint8_t key[kKeyLen]);

/// Derive Meshtastic node ID from WiFi MAC address (last 4 bytes).
NodeId nodeIdFromMac(const uint8_t mac[6]);

/// Encode a text string into Meshtastic protobuf Data payload.
/// Returns bytes written, or 0 on error.
size_t encodeTextPayload(const uint8_t* text, size_t textLen,
                         uint8_t* outBuf, size_t outBufMax);

/// Parse a decrypted protobuf Data payload. Extracts port number and payload pointer.
/// Returns true if parsing succeeded (even if no text field found).
bool decodeDataPayload(const uint8_t* data, size_t dataLen,
                       uint32_t& portOut,
                       const uint8_t*& payloadOut, size_t& payloadLenOut);

}  // namespace mesh::protocol
