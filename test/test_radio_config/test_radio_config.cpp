#include <unity.h>
#include "config/RadioConfig.h"

using namespace mesh::config;

// ── Meshtastic MediumFast EU preset verification ────────────────────────────
// These tests verify that our active radio config matches Meshtastic MediumFast
// EU (SF9, BW250, 869.525 MHz). If Meshtastic changes their presets, these
// tests will catch the drift.
// Reference: https://meshtastic.org/docs/overview/radio-settings/

void test_frequency() {
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 869.525F, kDefaultRadioConfig.frequencyMHz);
}

void test_bandwidth() {
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 250.0F, kDefaultRadioConfig.bandwidthKHz);
}

void test_spreading_factor() {
    // SF9 = MediumFast (SF11 would be LongFast)
    TEST_ASSERT_EQUAL_UINT8(9, kDefaultRadioConfig.spreadingFactor);
}

void test_coding_rate() {
    TEST_ASSERT_EQUAL_UINT8(5, kDefaultRadioConfig.codingRate);
}

void test_sync_word() {
    // 0x2B is the Meshtastic-specific LoRa sync word.
    // Using the wrong value means complete radio silence with the mesh.
    TEST_ASSERT_EQUAL_UINT8(0x2B, kDefaultRadioConfig.syncWord);
}

void test_preamble_length() {
    TEST_ASSERT_EQUAL_UINT16(16, kDefaultRadioConfig.preambleLength);
}

void test_output_power() {
    // 22 dBm is the max legal TX power for EU ISM 869 MHz band
    TEST_ASSERT_EQUAL_INT8(22, kDefaultRadioConfig.outputPowerDbm);
}

void test_current_limit() {
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 140.0F, kDefaultRadioConfig.currentLimitMa);
}

void test_tcxo_voltage() {
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 1.6F, kDefaultRadioConfig.tcxoVoltage);
}

// ── LongFast EU preset verification ─────────────────────────────────────────

void test_longfast_spreading_factor() {
    TEST_ASSERT_EQUAL_UINT8(11, kLongFastEuConfig.spreadingFactor);
}

void test_longfast_shares_common_params() {
    // LongFast differs only in SF — everything else matches MediumFast
    TEST_ASSERT_FLOAT_WITHIN(0.001F, kDefaultRadioConfig.frequencyMHz, kLongFastEuConfig.frequencyMHz);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, kDefaultRadioConfig.bandwidthKHz, kLongFastEuConfig.bandwidthKHz);
    TEST_ASSERT_EQUAL_UINT8(kDefaultRadioConfig.codingRate, kLongFastEuConfig.codingRate);
    TEST_ASSERT_EQUAL_UINT8(kDefaultRadioConfig.syncWord, kLongFastEuConfig.syncWord);
    TEST_ASSERT_EQUAL_UINT16(kDefaultRadioConfig.preambleLength, kLongFastEuConfig.preambleLength);
    TEST_ASSERT_EQUAL_INT8(kDefaultRadioConfig.outputPowerDbm, kLongFastEuConfig.outputPowerDbm);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_frequency);
    RUN_TEST(test_bandwidth);
    RUN_TEST(test_spreading_factor);
    RUN_TEST(test_coding_rate);
    RUN_TEST(test_sync_word);
    RUN_TEST(test_preamble_length);
    RUN_TEST(test_output_power);
    RUN_TEST(test_current_limit);
    RUN_TEST(test_tcxo_voltage);
    RUN_TEST(test_longfast_spreading_factor);
    RUN_TEST(test_longfast_shares_common_params);

    return UNITY_END();
}
