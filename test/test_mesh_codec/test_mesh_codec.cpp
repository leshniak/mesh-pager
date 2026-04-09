#include <unity.h>
#include "protocol/MeshCodec.h"

#include <cstring>

using namespace mesh::protocol;

// ── decodeBase64Key ─────────────────────────────────────────────────────────

void test_decode_base64_valid_key() {
    // "AQ==" decodes to a single byte 0x01, padded to 32 bytes — that's only 1 byte.
    // Use a proper 32-byte key encoded in base64 (44 chars).
    // 32 zero bytes in base64: AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=
    const char* b64 = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
    uint8_t key[kKeyLen] = {};
    TEST_ASSERT_TRUE(decodeBase64Key(b64, key));
    for (size_t i = 0; i < kKeyLen; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, key[i]);
    }
}

void test_decode_base64_real_key() {
    // Known base64 key from the example (32 bytes)
    const char* b64 = "Q3ee2Hb/+Zdi4r+QB4nnMXO7qmoAo9roXsxTVHBnWaE=";
    uint8_t key[kKeyLen] = {};
    TEST_ASSERT_TRUE(decodeBase64Key(b64, key));
    // First byte of the decoded key should be 0x43 ('C' from base64)
    TEST_ASSERT_EQUAL_UINT8(0x43, key[0]);
}

void test_decode_base64_short_key_fails() {
    // "AQ==" is only 1 byte — should fail (need exactly 32)
    const char* b64 = "AQ==";
    uint8_t key[kKeyLen] = {};
    TEST_ASSERT_FALSE(decodeBase64Key(b64, key));
}

void test_decode_base64_invalid_chars_fails() {
    const char* b64 = "!!!invalid!!!";
    uint8_t key[kKeyLen] = {};
    TEST_ASSERT_FALSE(decodeBase64Key(b64, key));
}

void test_decode_base64_empty_fails() {
    const char* b64 = "";
    uint8_t key[kKeyLen] = {};
    TEST_ASSERT_FALSE(decodeBase64Key(b64, key));
}

// ── computeChannelHash ──────────────────────────────────────────────────────

void test_channel_hash_deterministic() {
    uint8_t key[kKeyLen] = {};
    const uint8_t h1 = computeChannelHash("test", key);
    const uint8_t h2 = computeChannelHash("test", key);
    TEST_ASSERT_EQUAL_UINT8(h1, h2);
}

void test_channel_hash_different_names() {
    uint8_t key[kKeyLen] = {};
    const uint8_t h1 = computeChannelHash("alpha", key);
    const uint8_t h2 = computeChannelHash("beta", key);
    TEST_ASSERT_NOT_EQUAL(h1, h2);
}

void test_channel_hash_different_keys() {
    uint8_t key1[kKeyLen] = {};
    uint8_t key2[kKeyLen] = {};
    key2[0] = 0xFF;
    const uint8_t h1 = computeChannelHash("test", key1);
    const uint8_t h2 = computeChannelHash("test", key2);
    TEST_ASSERT_NOT_EQUAL(h1, h2);
}

void test_channel_hash_xor_property() {
    // XOR of name bytes XOR key bytes. For empty name with all-zero key: hash = 0
    uint8_t key[kKeyLen] = {};
    // Empty string: no name bytes to XOR, key is all zeros → hash = 0
    TEST_ASSERT_EQUAL_UINT8(0, computeChannelHash("", key));
}

void test_channel_hash_known_value() {
    // "Public" channel with default key "AQ==" (1 byte 0x01, but this
    // function takes a full 32-byte key array)
    uint8_t key[kKeyLen] = {};
    key[0] = 0x01;
    // XOR of "Public" = 'P'^'u'^'b'^'l'^'i'^'c' = 0x50^0x75^0x62^0x6C^0x69^0x63
    // = 0x50^0x75 = 0x25, ^0x62 = 0x47, ^0x6C = 0x2B, ^0x69 = 0x42, ^0x63 = 0x21
    // XOR of key = 0x01 (rest are 0)
    // hash = 0x21 ^ 0x01 = 0x20
    TEST_ASSERT_EQUAL_UINT8(0x20, computeChannelHash("Public", key));
}

// ── nodeIdFromMac ───────────────────────────────────────────────────────────

void test_node_id_from_mac() {
    // MAC: AA:BB:CC:DD:EE:FF → nodeId = 0xCCDDEEFF (bytes 2-5, big-endian)
    const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    TEST_ASSERT_EQUAL_UINT32(0xCCDDEEFF, nodeIdFromMac(mac));
}

void test_node_id_from_mac_zeros() {
    const uint8_t mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT32(0x00000000, nodeIdFromMac(mac));
}

void test_node_id_ignores_first_two_bytes() {
    const uint8_t mac1[6] = {0x00, 0x00, 0x11, 0x22, 0x33, 0x44};
    const uint8_t mac2[6] = {0xFF, 0xFF, 0x11, 0x22, 0x33, 0x44};
    TEST_ASSERT_EQUAL_UINT32(nodeIdFromMac(mac1), nodeIdFromMac(mac2));
}

// ── encodeTextPayload ───────────────────────────────────────────────────────

void test_encode_text_payload_basic() {
    const uint8_t text[] = "Hi";
    uint8_t buf[64] = {};
    const size_t len = encodeTextPayload(text, 2, buf, sizeof(buf));

    // Expected: 0x08 0x01 0x12 0x02 'H' 'i'
    TEST_ASSERT_EQUAL(6, len);
    TEST_ASSERT_EQUAL_UINT8(0x08, buf[0]);  // field 1 tag
    TEST_ASSERT_EQUAL_UINT8(0x01, buf[1]);  // TextMessage = 1
    TEST_ASSERT_EQUAL_UINT8(0x12, buf[2]);  // field 2 tag
    TEST_ASSERT_EQUAL_UINT8(0x02, buf[3]);  // length = 2
    TEST_ASSERT_EQUAL_UINT8('H', buf[4]);
    TEST_ASSERT_EQUAL_UINT8('i', buf[5]);
}

void test_encode_text_payload_empty_fails() {
    uint8_t buf[64];
    TEST_ASSERT_EQUAL(0, encodeTextPayload(nullptr, 0, buf, sizeof(buf)));
}

void test_encode_text_payload_too_long_fails() {
    uint8_t text[256] = {};
    uint8_t buf[512] = {};
    TEST_ASSERT_EQUAL(0, encodeTextPayload(text, 200, buf, sizeof(buf)));
}

void test_encode_text_payload_buffer_too_small() {
    const uint8_t text[] = "Hello";
    uint8_t buf[4] = {};  // too small for header + text
    TEST_ASSERT_EQUAL(0, encodeTextPayload(text, 5, buf, sizeof(buf)));
}

void test_encode_text_payload_max_length() {
    uint8_t text[kMaxTextLen];
    memset(text, 'A', kMaxTextLen);
    uint8_t buf[256] = {};
    const size_t len = encodeTextPayload(text, kMaxTextLen, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(4 + kMaxTextLen, len);  // 2 header bytes + 2 tag/len + text
}

// ── decodeDataPayload ───────────────────────────────────────────────────────

void test_decode_data_payload_basic() {
    // Encode then decode
    const uint8_t text[] = "test";
    uint8_t encoded[64] = {};
    const size_t encLen = encodeTextPayload(text, 4, encoded, sizeof(encoded));
    TEST_ASSERT_GREATER_THAN(0, encLen);

    uint32_t port = 0;
    const uint8_t* payload = nullptr;
    size_t payloadLen = 0;
    TEST_ASSERT_TRUE(decodeDataPayload(encoded, encLen, port, payload, payloadLen));
    TEST_ASSERT_EQUAL_UINT32(1, port);  // TextMessage
    TEST_ASSERT_EQUAL(4, payloadLen);
    TEST_ASSERT_EQUAL_MEMORY("test", payload, 4);
}

void test_decode_data_payload_empty_data() {
    uint32_t port = 99;
    const uint8_t* payload = nullptr;
    size_t payloadLen = 99;
    // Empty buffer should parse OK with no fields found
    TEST_ASSERT_TRUE(decodeDataPayload(nullptr, 0, port, payload, payloadLen));
    TEST_ASSERT_EQUAL_UINT32(0, port);
    TEST_ASSERT_NULL(payload);
    TEST_ASSERT_EQUAL(0, payloadLen);
}

void test_decode_data_payload_unknown_fields_skipped() {
    // Build a protobuf with field 1 (varint), field 99 (varint), field 2 (bytes)
    uint8_t data[] = {
        0x08, 0x01,           // field 1 = 1 (TextMessage)
        0xF8, 0x06, 0x42,    // field 99, wire type 0, varint value 0x42
        0x12, 0x02, 'O', 'K' // field 2, length 2, "OK"
    };
    uint32_t port = 0;
    const uint8_t* payload = nullptr;
    size_t payloadLen = 0;
    TEST_ASSERT_TRUE(decodeDataPayload(data, sizeof(data), port, payload, payloadLen));
    TEST_ASSERT_EQUAL_UINT32(1, port);
    TEST_ASSERT_EQUAL(2, payloadLen);
    TEST_ASSERT_EQUAL_MEMORY("OK", payload, 2);
}

void test_decode_data_payload_truncated_fails() {
    // Truncated varint
    uint8_t data[] = {0x08, 0x80};  // varint with continuation but no next byte
    uint32_t port = 0;
    const uint8_t* payload = nullptr;
    size_t payloadLen = 0;
    TEST_ASSERT_FALSE(decodeDataPayload(data, 1, port, payload, payloadLen));
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_decode_base64_valid_key);
    RUN_TEST(test_decode_base64_real_key);
    RUN_TEST(test_decode_base64_short_key_fails);
    RUN_TEST(test_decode_base64_invalid_chars_fails);
    RUN_TEST(test_decode_base64_empty_fails);

    RUN_TEST(test_channel_hash_deterministic);
    RUN_TEST(test_channel_hash_different_names);
    RUN_TEST(test_channel_hash_different_keys);
    RUN_TEST(test_channel_hash_xor_property);
    RUN_TEST(test_channel_hash_known_value);

    RUN_TEST(test_node_id_from_mac);
    RUN_TEST(test_node_id_from_mac_zeros);
    RUN_TEST(test_node_id_ignores_first_two_bytes);

    RUN_TEST(test_encode_text_payload_basic);
    RUN_TEST(test_encode_text_payload_empty_fails);
    RUN_TEST(test_encode_text_payload_too_long_fails);
    RUN_TEST(test_encode_text_payload_buffer_too_small);
    RUN_TEST(test_encode_text_payload_max_length);

    RUN_TEST(test_decode_data_payload_basic);
    RUN_TEST(test_decode_data_payload_empty_data);
    RUN_TEST(test_decode_data_payload_unknown_fields_skipped);
    RUN_TEST(test_decode_data_payload_truncated_fails);

    return UNITY_END();
}
