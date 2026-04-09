#include "protocol/MeshCrypto.h"

#include <mbedtls/aes.h>
#include <cstring>

namespace mesh::protocol {

void aesCtrCrypt(uint8_t* buffer, size_t len,
                 PacketId packetId, NodeId fromNode,
                 const uint8_t key[kKeyLen]) {
    uint8_t nonce[kNonceLen] = {};
    uint8_t streamBlock[kNonceLen] = {};
    size_t ncOff = 0;

    const uint64_t packetIdU64 = static_cast<uint64_t>(packetId);
    std::memcpy(nonce, &packetIdU64, 8);
    std::memcpy(nonce + 8, &fromNode, 4);

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, key, 256);
    mbedtls_aes_crypt_ctr(&ctx, len, &ncOff, nonce, streamBlock, buffer, buffer);
    mbedtls_aes_free(&ctx);
}

}  // namespace mesh::protocol
