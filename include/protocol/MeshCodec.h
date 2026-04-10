#pragma once

/// Meshtastic protobuf encoding/decoding and identity helpers.
///
/// Meshtastic uses Protocol Buffers (protobuf) to serialize its "Data" message,
/// which wraps the application payload. This module provides minimal hand-rolled
/// protobuf encoding and decoding — no protobuf library dependency.
///
/// Data message protobuf layout (fields we use):
///   field 1 (varint)            — portnum (application port, e.g. 1 = TextMessage)
///   field 2 (length-delimited)  — application payload bytes (e.g. UTF-8 text)
///
/// Channel hash: a single byte that identifies the channel without revealing
/// the channel name or key. Computed as XOR(name_bytes) XOR XOR(key_bytes).
/// Used by receivers to quickly discard packets from other channels before
/// attempting decryption.
///
/// Node ID: derived from the device's WiFi MAC address (last 4 bytes), matching
/// the Meshtastic convention for ESP32 devices.

#include "protocol/MeshTypes.h"

namespace mesh::protocol {

/// Decode a base64-encoded 32-byte AES-256 channel key.
/// The key is configured in secrets.h and shared across all nodes on the channel.
bool decodeBase64Key(const char* base64, uint8_t outKey[kKeyLen]);

/// Compute the Meshtastic channel hash byte from channel name and key.
/// This is a single-byte fingerprint: XOR of all name bytes XOR all key bytes.
/// Receivers compare this against their own channel hash to filter packets cheaply.
uint8_t computeChannelHash(const char* channelName, const uint8_t key[kKeyLen]);

/// Derive Meshtastic node ID from WiFi MAC address (bytes [2..5]).
/// Matches the convention used by ESP32 Meshtastic devices.
NodeId nodeIdFromMac(const uint8_t mac[kMacLen]);

/// Encode a text string into a Meshtastic protobuf Data payload.
///
/// Output format (protobuf):
///   0x08 <portnum>              — field 1, varint: PortNum::TextMessage (1)
///   0x12 <length> <text bytes>  — field 2, length-delimited: message text
///
/// Returns bytes written to outBuf, or 0 on error.
size_t encodeTextPayload(const uint8_t* text, size_t textLen,
                         uint8_t* outBuf, size_t outBufMax);

/// Parse a decrypted protobuf Data payload.
///
/// Extracts the port number (field 1) and a pointer into the payload bytes (field 2).
/// Unknown fields are skipped, making this forward-compatible with newer Meshtastic
/// protobuf additions. Returns true if parsing succeeded (even if the message type
/// isn't one we handle — portOut will indicate the type).
bool decodeDataPayload(const uint8_t* data, size_t dataLen,
                       uint32_t& portOut,
                       const uint8_t*& payloadOut, size_t& payloadLenOut);

}  // namespace mesh::protocol
