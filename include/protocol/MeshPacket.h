#pragma once

#include "protocol/MeshTypes.h"

namespace mesh::protocol {

/// Build a complete mesh frame (header + encrypted payload) ready for TX.
/// Encrypts plainPayload in-place, then prepends the header.
/// Returns total frame length, or 0 on error.
size_t buildPacket(const PacketHeader& header,
                   uint8_t* plainPayload, size_t payloadLen,
                   const uint8_t key[kKeyLen],
                   uint8_t* outFrame, size_t outFrameMax);

/// Parse a received mesh frame into header + decrypted text.
/// outText is null-terminated, sanitized to printable ASCII.
/// Returns MeshError::Ok on success.
MeshError parsePacket(const uint8_t* frame, size_t frameLen,
                      const uint8_t key[kKeyLen],
                      PacketHeader& outHeader,
                      char* outText, size_t outTextMax, size_t& outTextLen);

}  // namespace mesh::protocol
