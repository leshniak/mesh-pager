#include <unity.h>
#include "protocol/MeshTypes.h"

using namespace mesh::protocol;

// ── putLe32 / getLe32 roundtrip ─────────────────────────────────────────────

void test_le32_roundtrip_zero() {
    uint8_t buf[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    putLe32(buf, 0);
    TEST_ASSERT_EQUAL_UINT32(0, getLe32(buf));
}

void test_le32_roundtrip_max() {
    uint8_t buf[4];
    putLe32(buf, 0xFFFFFFFF);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFF, getLe32(buf));
}

void test_le32_roundtrip_arbitrary() {
    uint8_t buf[4];
    putLe32(buf, 0xDEADBEEF);
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEF, getLe32(buf));
}

void test_le32_byte_order() {
    uint8_t buf[4];
    putLe32(buf, 0x04030201);
    // Little-endian: least significant byte first
    TEST_ASSERT_EQUAL_UINT8(0x01, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x02, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x03, buf[2]);
    TEST_ASSERT_EQUAL_UINT8(0x04, buf[3]);
}

// ── makeMeshFlags ───────────────────────────────────────────────────────────

void test_flags_default() {
    // hopLimit=3, no ack, no mqtt, hopStart=3
    const uint8_t flags = makeMeshFlags(3, false, false, 3);
    // hopLimit[2:0] = 0b011 = 3
    // wantAck[3]    = 0
    // viaMqtt[4]    = 0
    // hopStart[7:5] = 0b011 << 5 = 0x60
    TEST_ASSERT_EQUAL_UINT8(0x63, flags);
}

void test_flags_want_ack() {
    const uint8_t flags = makeMeshFlags(3, true, false, 3);
    TEST_ASSERT_TRUE(flags & 0x08);  // bit 3 set
}

void test_flags_via_mqtt() {
    const uint8_t flags = makeMeshFlags(3, false, true, 3);
    TEST_ASSERT_TRUE(flags & 0x10);  // bit 4 set
}

void test_flags_hop_limit_range() {
    // hopLimit is 3 bits — values > 7 should be masked
    const uint8_t flags = makeMeshFlags(0xFF, false, false, 0);
    TEST_ASSERT_EQUAL_UINT8(0x07, flags & 0x07);  // only low 3 bits
}

void test_flags_hop_start_range() {
    const uint8_t flags = makeMeshFlags(0, false, false, 0xFF);
    TEST_ASSERT_EQUAL_UINT8(0xE0, flags & 0xE0);  // only top 3 bits
}

void test_flags_all_set() {
    const uint8_t flags = makeMeshFlags(7, true, true, 7);
    TEST_ASSERT_EQUAL_UINT8(0xFF, flags);
}

void test_flags_all_clear() {
    const uint8_t flags = makeMeshFlags(0, false, false, 0);
    TEST_ASSERT_EQUAL_UINT8(0x00, flags);
}

// ── Constants ───────────────────────────────────────────────────────────────

void test_broadcast_addr() {
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFF, kBroadcastAddr);
}

void test_header_length() {
    TEST_ASSERT_EQUAL(16, kMeshHeaderLen);
}

void test_key_length() {
    TEST_ASSERT_EQUAL(32, kKeyLen);
}

void test_nonce_length() {
    TEST_ASSERT_EQUAL(16, kNonceLen);
}

// ── PacketHeader defaults ───────────────────────────────────────────────────

void test_header_defaults() {
    PacketHeader hdr;
    TEST_ASSERT_EQUAL_UINT32(kBroadcastAddr, hdr.dest);
    TEST_ASSERT_EQUAL_UINT32(0, hdr.source);
    TEST_ASSERT_EQUAL_UINT32(0, hdr.packetId);
    TEST_ASSERT_EQUAL_UINT8(0, hdr.flags);
    TEST_ASSERT_EQUAL_UINT8(0, hdr.channelHash);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_le32_roundtrip_zero);
    RUN_TEST(test_le32_roundtrip_max);
    RUN_TEST(test_le32_roundtrip_arbitrary);
    RUN_TEST(test_le32_byte_order);

    RUN_TEST(test_flags_default);
    RUN_TEST(test_flags_want_ack);
    RUN_TEST(test_flags_via_mqtt);
    RUN_TEST(test_flags_hop_limit_range);
    RUN_TEST(test_flags_hop_start_range);
    RUN_TEST(test_flags_all_set);
    RUN_TEST(test_flags_all_clear);

    RUN_TEST(test_broadcast_addr);
    RUN_TEST(test_header_length);
    RUN_TEST(test_key_length);
    RUN_TEST(test_nonce_length);

    RUN_TEST(test_header_defaults);

    return UNITY_END();
}
