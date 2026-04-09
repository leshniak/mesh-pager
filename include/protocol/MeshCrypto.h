#pragma once

#include "protocol/MeshTypes.h"

namespace mesh::protocol {

/// AES-256-CTR encrypt/decrypt in-place using Meshtastic nonce format.
/// Nonce = packetId (8 bytes LE) + fromNode (4 bytes LE) + zeros (4 bytes).
/// Encrypt and decrypt are the same operation (CTR mode).
void aesCtrCrypt(uint8_t* buffer, size_t len,
                 PacketId packetId, NodeId fromNode,
                 const uint8_t key[kKeyLen]);

}  // namespace mesh::protocol
