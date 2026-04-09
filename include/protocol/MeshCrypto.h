#pragma once

/// Meshtastic packet encryption using AES-256-CTR.
///
/// All Meshtastic payloads are encrypted with a shared channel key. The nonce is
/// derived deterministically from the packet ID and sender node ID, ensuring each
/// packet uses a unique keystream. CTR mode is symmetric — the same function
/// encrypts and decrypts.
///
/// Nonce layout (16 bytes):
///   [0..7]   packetId as uint64 LE (upper 4 bytes are always zero)
///   [8..11]  fromNode as uint32 LE
///   [12..15] zero padding

#include "protocol/MeshTypes.h"

namespace mesh::protocol {

/// AES-256-CTR encrypt/decrypt in-place using Meshtastic nonce format.
/// The same call encrypts (before TX) and decrypts (after RX) because CTR mode
/// XORs plaintext with a keystream — applying it twice recovers the original.
void aesCtrCrypt(uint8_t* buffer, size_t len,
                 PacketId packetId, NodeId fromNode,
                 const uint8_t key[kKeyLen]);

}  // namespace mesh::protocol
