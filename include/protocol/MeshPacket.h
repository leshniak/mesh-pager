#pragma once

/// High-level packet building and parsing.
///
/// TX flow:  plaintext → encodeTextPayload() → buildPacket() → encrypted frame → radio.transmit()
/// RX flow:  radio.receive() → raw frame → parsePacket() → header + decrypted text
///
/// buildPacket() encrypts the protobuf payload in-place using the channel key,
/// then prepends the 16-byte mesh header. The result is a complete over-the-air frame.
///
/// parsePacket() reverses the process: extracts the header, decrypts the payload,
/// decodes the protobuf, and extracts the text if it's a TextMessage. Non-text
/// messages (Routing, Position, etc.) return Ok with textLen=0.

#include "protocol/MeshTypes.h"

namespace mesh::protocol {

/// Build a complete mesh frame (header + encrypted payload) ready for TX.
///
/// WARNING: plainPayload is encrypted in-place — the buffer is modified.
/// The encrypted payload is then copied after the header into outFrame.
///
/// Returns total frame length (header + payload), or 0 on error.
size_t buildPacket(const PacketHeader& header,
                   uint8_t* plainPayload, size_t payloadLen,
                   const uint8_t key[kKeyLen],
                   uint8_t* outFrame, size_t outFrameMax);

/// Parse a received mesh frame: extract header, decrypt payload, decode text.
///
/// On success (MeshError::Ok):
///   - outHeader is populated with all header fields
///   - If the payload is a TextMessage: outText contains the sanitized ASCII text
///   - If the payload is another port type: outTextLen == 0 (not an error)
///
/// Text is sanitized to printable ASCII (0x20..0x7E), with non-printable bytes
/// replaced by '.'. This prevents rendering issues from malformed UTF-8 or
/// control characters.
MeshError parsePacket(const uint8_t* frame, size_t frameLen,
                      const uint8_t key[kKeyLen],
                      PacketHeader& outHeader,
                      char* outText, size_t outTextMax, size_t& outTextLen);

}  // namespace mesh::protocol
