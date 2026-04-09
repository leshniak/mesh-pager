#include <unity.h>
#include "protocol/MeshCrypto.h"

#include <cstring>

using namespace mesh::protocol;

// ── AES-CTR encrypt/decrypt symmetry ────────────────────────────────────────

void test_encrypt_decrypt_roundtrip() {
    uint8_t key[kKeyLen] = {};
    key[0] = 0x42;  // Non-zero key

    const char original[] = "Hello, Meshtastic!";
    uint8_t buffer[64];
    memcpy(buffer, original, sizeof(original));

    const PacketId packetId = 0x1234;
    const NodeId fromNode = 0xDEADBEEF;

    // Encrypt
    aesCtrCrypt(buffer, sizeof(original), packetId, fromNode, key);

    // Encrypted data should differ from original
    TEST_ASSERT_NOT_EQUAL(0, memcmp(buffer, original, sizeof(original)));

    // Decrypt (same operation — CTR mode is symmetric)
    aesCtrCrypt(buffer, sizeof(original), packetId, fromNode, key);

    // Should be back to original
    TEST_ASSERT_EQUAL_MEMORY(original, buffer, sizeof(original));
}

void test_encrypt_different_packet_ids_produce_different_ciphertext() {
    uint8_t key[kKeyLen] = {};
    key[0] = 0x01;

    const char text[] = "same text";
    uint8_t buf1[32], buf2[32];
    memcpy(buf1, text, sizeof(text));
    memcpy(buf2, text, sizeof(text));

    aesCtrCrypt(buf1, sizeof(text), 1, 0x1000, key);
    aesCtrCrypt(buf2, sizeof(text), 2, 0x1000, key);

    // Different nonces → different ciphertext
    TEST_ASSERT_NOT_EQUAL(0, memcmp(buf1, buf2, sizeof(text)));
}

void test_encrypt_different_nodes_produce_different_ciphertext() {
    uint8_t key[kKeyLen] = {};
    key[0] = 0x01;

    const char text[] = "same text";
    uint8_t buf1[32], buf2[32];
    memcpy(buf1, text, sizeof(text));
    memcpy(buf2, text, sizeof(text));

    aesCtrCrypt(buf1, sizeof(text), 42, 0x1000, key);
    aesCtrCrypt(buf2, sizeof(text), 42, 0x2000, key);

    TEST_ASSERT_NOT_EQUAL(0, memcmp(buf1, buf2, sizeof(text)));
}

void test_encrypt_different_keys_produce_different_ciphertext() {
    uint8_t key1[kKeyLen] = {};
    uint8_t key2[kKeyLen] = {};
    key1[0] = 0x01;
    key2[0] = 0x02;

    const char text[] = "same text";
    uint8_t buf1[32], buf2[32];
    memcpy(buf1, text, sizeof(text));
    memcpy(buf2, text, sizeof(text));

    aesCtrCrypt(buf1, sizeof(text), 42, 0x1000, key1);
    aesCtrCrypt(buf2, sizeof(text), 42, 0x1000, key2);

    TEST_ASSERT_NOT_EQUAL(0, memcmp(buf1, buf2, sizeof(text)));
}

void test_encrypt_wrong_key_decrypts_to_garbage() {
    uint8_t keyA[kKeyLen] = {};
    uint8_t keyB[kKeyLen] = {};
    keyA[0] = 0x01;
    keyB[0] = 0x02;

    const char original[] = "secret message";
    uint8_t buffer[64];
    memcpy(buffer, original, sizeof(original));

    // Encrypt with key A
    aesCtrCrypt(buffer, sizeof(original), 1, 0x100, keyA);

    // Decrypt with key B
    aesCtrCrypt(buffer, sizeof(original), 1, 0x100, keyB);

    // Should NOT produce the original text
    TEST_ASSERT_NOT_EQUAL(0, memcmp(buffer, original, sizeof(original)));
}

void test_encrypt_zero_length() {
    uint8_t key[kKeyLen] = {};
    uint8_t buffer[1] = {0x42};
    // Zero-length encryption should be a no-op
    aesCtrCrypt(buffer, 0, 1, 1, key);
    TEST_ASSERT_EQUAL_UINT8(0x42, buffer[0]);  // unchanged
}

void test_encrypt_single_byte() {
    uint8_t key[kKeyLen] = {};
    key[0] = 0xFF;

    uint8_t buf = 0x00;
    aesCtrCrypt(&buf, 1, 100, 200, key);
    // Should be encrypted (non-zero with this key)
    uint8_t encrypted = buf;

    // Decrypt
    aesCtrCrypt(&buf, 1, 100, 200, key);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf);  // back to original
}

void test_encrypt_large_payload() {
    uint8_t key[kKeyLen];
    memset(key, 0xAB, kKeyLen);

    uint8_t buffer[kMaxEncryptedLen];
    uint8_t original[kMaxEncryptedLen];
    memset(original, 0x55, kMaxEncryptedLen);
    memcpy(buffer, original, kMaxEncryptedLen);

    aesCtrCrypt(buffer, kMaxEncryptedLen, 999, 0x12345678, key);
    TEST_ASSERT_NOT_EQUAL(0, memcmp(buffer, original, kMaxEncryptedLen));

    aesCtrCrypt(buffer, kMaxEncryptedLen, 999, 0x12345678, key);
    TEST_ASSERT_EQUAL_MEMORY(original, buffer, kMaxEncryptedLen);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_encrypt_decrypt_roundtrip);
    RUN_TEST(test_encrypt_different_packet_ids_produce_different_ciphertext);
    RUN_TEST(test_encrypt_different_nodes_produce_different_ciphertext);
    RUN_TEST(test_encrypt_different_keys_produce_different_ciphertext);
    RUN_TEST(test_encrypt_wrong_key_decrypts_to_garbage);
    RUN_TEST(test_encrypt_zero_length);
    RUN_TEST(test_encrypt_single_byte);
    RUN_TEST(test_encrypt_large_payload);

    return UNITY_END();
}
