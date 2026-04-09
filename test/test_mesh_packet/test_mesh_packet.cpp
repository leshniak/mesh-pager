#include <unity.h>
#include "protocol/MeshPacket.h"
#include "protocol/MeshCodec.h"
#include "protocol/MeshTypes.h"

#include <cstring>

using namespace mesh::protocol;

// Shared test key (32 bytes, base64: Q3ee2Hb/+Zdi4r+QB4nnMXO7qmoAo9roXsxTVHBnWaE=)
static uint8_t testKey[kKeyLen];
static bool keyDecoded = false;

static void ensureKey() {
    if (!keyDecoded) {
        keyDecoded = decodeBase64Key(
            "Q3ee2Hb/+Zdi4r+QB4nnMXO7qmoAo9roXsxTVHBnWaE=", testKey);
    }
}

// ── buildPacket + parsePacket roundtrip ─────────────────────────────────────

void test_build_parse_roundtrip() {
    ensureKey();
    TEST_ASSERT_TRUE(keyDecoded);

    // Encode a text payload
    const uint8_t text[] = "Hello mesh!";
    uint8_t payload[64];
    const size_t payloadLen = encodeTextPayload(text, strlen((const char*)text),
                                                 payload, sizeof(payload));
    TEST_ASSERT_GREATER_THAN(0, payloadLen);

    // Build packet
    PacketHeader hdr;
    hdr.dest = kBroadcastAddr;
    hdr.source = 0xAABBCCDD;
    hdr.packetId = 0x1234;
    hdr.flags = makeMeshFlags(3, false, false, 3);
    hdr.channelHash = 0x42;

    uint8_t frame[256];
    const size_t frameLen = buildPacket(hdr, payload, payloadLen, testKey,
                                         frame, sizeof(frame));
    TEST_ASSERT_GREATER_THAN(kMeshHeaderLen, frameLen);

    // Parse packet
    PacketHeader outHdr;
    char outText[128];
    size_t outTextLen = 0;
    const MeshError err = parsePacket(frame, frameLen, testKey,
                                       outHdr, outText, sizeof(outText), outTextLen);

    TEST_ASSERT_EQUAL(MeshError::Ok, err);
    TEST_ASSERT_EQUAL_UINT32(kBroadcastAddr, outHdr.dest);
    TEST_ASSERT_EQUAL_UINT32(0xAABBCCDD, outHdr.source);
    TEST_ASSERT_EQUAL_UINT32(0x1234, outHdr.packetId);
    TEST_ASSERT_EQUAL_UINT8(0x42, outHdr.channelHash);
    TEST_ASSERT_EQUAL(strlen("Hello mesh!"), outTextLen);
    TEST_ASSERT_EQUAL_STRING("Hello mesh!", outText);
}

void test_build_parse_max_text() {
    ensureKey();

    char longText[kMaxTextLen + 1];
    memset(longText, 'X', kMaxTextLen);
    longText[kMaxTextLen] = '\0';

    uint8_t payload[256];
    const size_t payloadLen = encodeTextPayload(
        (const uint8_t*)longText, kMaxTextLen, payload, sizeof(payload));
    TEST_ASSERT_GREATER_THAN(0, payloadLen);

    PacketHeader hdr;
    hdr.source = 0x100;
    hdr.packetId = 42;

    uint8_t frame[512];
    const size_t frameLen = buildPacket(hdr, payload, payloadLen, testKey,
                                         frame, sizeof(frame));
    TEST_ASSERT_GREATER_THAN(0, frameLen);

    PacketHeader outHdr;
    char outText[256];
    size_t outTextLen = 0;
    TEST_ASSERT_EQUAL(MeshError::Ok,
                      parsePacket(frame, frameLen, testKey, outHdr,
                                  outText, sizeof(outText), outTextLen));
    TEST_ASSERT_EQUAL(kMaxTextLen, outTextLen);
}

// ── Header preservation ─────────────────────────────────────────────────────

void test_header_fields_preserved() {
    ensureKey();

    uint8_t payload[] = {0x08, 0x01, 0x12, 0x01, 'A'};  // hand-crafted protobuf
    PacketHeader hdr;
    hdr.dest = 0x11223344;
    hdr.source = 0x55667788;
    hdr.packetId = 0xDEAD;
    hdr.flags = 0x63;
    hdr.channelHash = 0xAB;
    hdr.nextHop = 0x0F;
    hdr.relayNode = 0xF0;

    uint8_t frame[64];
    const size_t frameLen = buildPacket(hdr, payload, sizeof(payload), testKey,
                                         frame, sizeof(frame));

    PacketHeader outHdr;
    char outText[32];
    size_t outTextLen = 0;
    parsePacket(frame, frameLen, testKey, outHdr, outText, sizeof(outText), outTextLen);

    TEST_ASSERT_EQUAL_UINT32(0x11223344, outHdr.dest);
    TEST_ASSERT_EQUAL_UINT32(0x55667788, outHdr.source);
    TEST_ASSERT_EQUAL_UINT32(0xDEAD, outHdr.packetId);
    TEST_ASSERT_EQUAL_UINT8(0x63, outHdr.flags);
    TEST_ASSERT_EQUAL_UINT8(0xAB, outHdr.channelHash);
    TEST_ASSERT_EQUAL_UINT8(0x0F, outHdr.nextHop);
    TEST_ASSERT_EQUAL_UINT8(0xF0, outHdr.relayNode);
}

// ── Error cases ─────────────────────────────────────────────────────────────

void test_parse_too_short() {
    ensureKey();
    uint8_t frame[10] = {};
    PacketHeader hdr;
    char text[32];
    size_t textLen = 0;
    TEST_ASSERT_EQUAL(MeshError::PacketTooShort,
                      parsePacket(frame, 10, testKey, hdr, text, sizeof(text), textLen));
}

void test_parse_minimum_valid_length() {
    ensureKey();
    // 16 header + 2 bytes payload minimum
    uint8_t frame[18] = {};
    PacketHeader hdr;
    char text[32];
    size_t textLen = 0;
    // This may decode or fail depending on the payload content, but shouldn't crash
    MeshError err = parsePacket(frame, 18, testKey, hdr, text, sizeof(text), textLen);
    // Either Ok (with empty text) or DecodeFailed — but not PacketTooShort
    TEST_ASSERT_NOT_EQUAL(MeshError::PacketTooShort, err);
}

void test_build_buffer_too_small() {
    ensureKey();
    uint8_t payload[] = {0x08, 0x01, 0x12, 0x01, 'A'};
    PacketHeader hdr;
    uint8_t frame[10];  // too small for header + payload
    TEST_ASSERT_EQUAL(0, buildPacket(hdr, payload, sizeof(payload), testKey,
                                      frame, sizeof(frame)));
}

// ── ASCII sanitization ──────────────────────────────────────────────────────

void test_parse_sanitizes_non_printable() {
    ensureKey();

    // Build a payload with control chars embedded
    uint8_t rawText[] = {'H', 'i', 0x01, 0x7F, '!'};
    uint8_t payload[64];
    const size_t payloadLen = encodeTextPayload(rawText, 5, payload, sizeof(payload));

    PacketHeader hdr;
    hdr.source = 0x100;
    hdr.packetId = 7;

    uint8_t frame[128];
    const size_t frameLen = buildPacket(hdr, payload, payloadLen, testKey,
                                         frame, sizeof(frame));

    PacketHeader outHdr;
    char outText[64];
    size_t outTextLen = 0;
    TEST_ASSERT_EQUAL(MeshError::Ok,
                      parsePacket(frame, frameLen, testKey, outHdr,
                                  outText, sizeof(outText), outTextLen));
    TEST_ASSERT_EQUAL(5, outTextLen);
    TEST_ASSERT_EQUAL('H', outText[0]);
    TEST_ASSERT_EQUAL('i', outText[1]);
    TEST_ASSERT_EQUAL('.', outText[2]);  // 0x01 → '.'
    TEST_ASSERT_EQUAL('.', outText[3]);  // 0x7F → '.'
    TEST_ASSERT_EQUAL('!', outText[4]);
}

// ── Wrong key produces garbage (not original text) ──────────────────────────

void test_parse_wrong_key_fails() {
    ensureKey();

    const uint8_t text[] = "secret";
    uint8_t payload[32];
    const size_t payloadLen = encodeTextPayload(text, 6, payload, sizeof(payload));

    PacketHeader hdr;
    hdr.source = 0x100;
    hdr.packetId = 99;

    uint8_t frame[128];
    const size_t frameLen = buildPacket(hdr, payload, payloadLen, testKey,
                                         frame, sizeof(frame));

    // Try to parse with wrong key
    uint8_t wrongKey[kKeyLen] = {};
    wrongKey[0] = 0xFF;

    PacketHeader outHdr;
    char outText[64];
    size_t outTextLen = 0;
    MeshError err = parsePacket(frame, frameLen, wrongKey, outHdr,
                                 outText, sizeof(outText), outTextLen);

    // Wrong key → either decode failure or garbled text (not "secret")
    if (err == MeshError::Ok && outTextLen > 0) {
        TEST_ASSERT_NOT_EQUAL(0, memcmp(outText, "secret", 6));
    }
    // If DecodeFailed, that's also acceptable
}

// ── Non-text port produces zero textLen ─────────────────────────────────────

void test_parse_non_text_port_returns_empty() {
    ensureKey();

    // Hand-craft a protobuf with portNum=4 (Routing) and some payload
    uint8_t payload[] = {0x08, 0x04, 0x12, 0x02, 0x00, 0x00};

    PacketHeader hdr;
    hdr.source = 0x200;
    hdr.packetId = 50;

    uint8_t frame[128];
    const size_t frameLen = buildPacket(hdr, payload, sizeof(payload), testKey,
                                         frame, sizeof(frame));

    PacketHeader outHdr;
    char outText[64];
    size_t outTextLen = 99;  // intentionally non-zero
    MeshError err = parsePacket(frame, frameLen, testKey, outHdr,
                                 outText, sizeof(outText), outTextLen);
    TEST_ASSERT_EQUAL(MeshError::Ok, err);
    TEST_ASSERT_EQUAL(0, outTextLen);  // non-text port → no text extracted
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_build_parse_roundtrip);
    RUN_TEST(test_build_parse_max_text);
    RUN_TEST(test_header_fields_preserved);
    RUN_TEST(test_parse_too_short);
    RUN_TEST(test_parse_minimum_valid_length);
    RUN_TEST(test_build_buffer_too_small);
    RUN_TEST(test_parse_sanitizes_non_printable);
    RUN_TEST(test_parse_wrong_key_fails);
    RUN_TEST(test_parse_non_text_port_returns_empty);

    return UNITY_END();
}
