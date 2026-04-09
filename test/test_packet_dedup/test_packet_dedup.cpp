#include <unity.h>
#include "protocol/PacketDedup.h"

using mesh::protocol::PacketDedup;

// Use small capacity and short expiry for easier testing
using TestDedup = PacketDedup<4, 1000>;

void test_new_packet_is_not_duplicate() {
    TestDedup d;
    TEST_ASSERT_FALSE(d.isDuplicate(0xAA, 1, 100));
}

void test_same_packet_is_duplicate() {
    TestDedup d;
    d.isDuplicate(0xAA, 1, 100);
    TEST_ASSERT_TRUE(d.isDuplicate(0xAA, 1, 200));
}

void test_different_source_is_not_duplicate() {
    TestDedup d;
    d.isDuplicate(0xAA, 1, 100);
    TEST_ASSERT_FALSE(d.isDuplicate(0xBB, 1, 200));
}

void test_different_packetId_is_not_duplicate() {
    TestDedup d;
    d.isDuplicate(0xAA, 1, 100);
    TEST_ASSERT_FALSE(d.isDuplicate(0xAA, 2, 200));
}

void test_expired_entry_is_not_duplicate() {
    TestDedup d;
    d.isDuplicate(0xAA, 1, 100);
    // 1100ms later, exceeds 1000ms expiry
    TEST_ASSERT_FALSE(d.isDuplicate(0xAA, 1, 1200));
}

void test_entry_just_before_expiry_is_still_duplicate() {
    TestDedup d;
    d.isDuplicate(0xAA, 1, 100);
    // 999ms later, still within expiry
    TEST_ASSERT_TRUE(d.isDuplicate(0xAA, 1, 1099));
}

void test_capacity_overflow_evicts_oldest() {
    TestDedup d;  // capacity = 4
    d.isDuplicate(0xAA, 1, 100);  // slot 0
    d.isDuplicate(0xAA, 2, 200);  // slot 1
    d.isDuplicate(0xAA, 3, 300);  // slot 2
    d.isDuplicate(0xAA, 4, 400);  // slot 3

    // 5th entry should evict oldest (packetId=1 at t=100)
    d.isDuplicate(0xAA, 5, 500);

    // Evicted entry is no longer a duplicate
    TEST_ASSERT_FALSE(d.isDuplicate(0xAA, 1, 600));
    // Recent entries still cached
    TEST_ASSERT_TRUE(d.isDuplicate(0xAA, 5, 600));
}

void test_multiple_sources_tracked() {
    TestDedup d;
    d.isDuplicate(0xAA, 1, 100);
    d.isDuplicate(0xBB, 1, 100);
    d.isDuplicate(0xCC, 2, 100);

    TEST_ASSERT_TRUE(d.isDuplicate(0xAA, 1, 200));
    TEST_ASSERT_TRUE(d.isDuplicate(0xBB, 1, 200));
    TEST_ASSERT_TRUE(d.isDuplicate(0xCC, 2, 200));
    TEST_ASSERT_FALSE(d.isDuplicate(0xCC, 1, 200));
}

void test_expiry_frees_slots_for_reuse() {
    TestDedup d;  // capacity = 4
    d.isDuplicate(0xAA, 1, 100);
    d.isDuplicate(0xAA, 2, 100);
    d.isDuplicate(0xAA, 3, 100);
    d.isDuplicate(0xAA, 4, 100);

    // All 4 slots full, but all expired at t=1200
    // New entries should fit without evicting unexpired data
    TEST_ASSERT_FALSE(d.isDuplicate(0xBB, 10, 1200));
    TEST_ASSERT_FALSE(d.isDuplicate(0xBB, 11, 1200));
    TEST_ASSERT_TRUE(d.isDuplicate(0xBB, 10, 1300));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_new_packet_is_not_duplicate);
    RUN_TEST(test_same_packet_is_duplicate);
    RUN_TEST(test_different_source_is_not_duplicate);
    RUN_TEST(test_different_packetId_is_not_duplicate);
    RUN_TEST(test_expired_entry_is_not_duplicate);
    RUN_TEST(test_entry_just_before_expiry_is_still_duplicate);
    RUN_TEST(test_capacity_overflow_evicts_oldest);
    RUN_TEST(test_multiple_sources_tracked);
    RUN_TEST(test_expiry_frees_slots_for_reuse);
    return UNITY_END();
}
