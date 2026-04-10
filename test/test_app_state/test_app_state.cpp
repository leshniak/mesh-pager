#include <unity.h>
#include "app/AppState.h"
#include "config/AppConfig.h"

using namespace mesh::app;

// Helper: create a default InputEvents (all false/zero)
static InputEvents noEvents() {
    return InputEvents{};
}

// ── Basic transitions from Idle ─────────────────────────────────────────────

void test_idle_stays_idle_with_no_events() {
    TEST_ASSERT_EQUAL(State::Idle, nextState(State::Idle, noEvents()));
}

void test_single_click_transmits() {
    auto e = noEvents();
    e.singleClick = true;
    TEST_ASSERT_EQUAL(State::Transmitting, nextState(State::Idle, e));
}

void test_hold_complete_transmits() {
    auto e = noEvents();
    e.holdComplete = true;
    TEST_ASSERT_EQUAL(State::Transmitting, nextState(State::Idle, e));
}

void test_long_press_powers_off() {
    auto e = noEvents();
    e.longPress = true;
    TEST_ASSERT_EQUAL(State::PoweringOff, nextState(State::Idle, e));
}

void test_rx_ready_receives() {
    auto e = noEvents();
    e.rxReady = true;
    TEST_ASSERT_EQUAL(State::Receiving, nextState(State::Idle, e));
}

void test_double_click_stays_idle() {
    // doubleClick is handled by caller, not state machine
    auto e = noEvents();
    e.doubleClick = true;
    TEST_ASSERT_EQUAL(State::Idle, nextState(State::Idle, e));
}

// ── Sleep timeout ───────────────────────────────────────────────────────────

void test_sleep_timeout_when_not_charging() {
    auto e = noEvents();
    e.timeSinceLastActionMs = mesh::config::kSleepTimeoutMs;
    e.isCharging = false;
    TEST_ASSERT_EQUAL(State::EnteringSleep, nextState(State::Idle, e));
}

void test_no_sleep_when_charging() {
    auto e = noEvents();
    e.timeSinceLastActionMs = mesh::config::kSleepTimeoutMs + 10000;
    e.isCharging = true;
    TEST_ASSERT_EQUAL(State::Idle, nextState(State::Idle, e));
}

void test_no_sleep_before_timeout() {
    auto e = noEvents();
    e.timeSinceLastActionMs = mesh::config::kSleepTimeoutMs - 1;
    e.isCharging = false;
    TEST_ASSERT_EQUAL(State::Idle, nextState(State::Idle, e));
}

// ── Priority ordering ───────────────────────────────────────────────────────

void test_power_off_beats_sleep() {
    auto e = noEvents();
    e.longPress = true;
    e.timeSinceLastActionMs = mesh::config::kSleepTimeoutMs;
    e.isCharging = false;
    TEST_ASSERT_EQUAL(State::PoweringOff, nextState(State::Idle, e));
}

void test_power_off_beats_transmit() {
    auto e = noEvents();
    e.longPress = true;
    e.singleClick = true;
    TEST_ASSERT_EQUAL(State::PoweringOff, nextState(State::Idle, e));
}

void test_sleep_beats_transmit() {
    auto e = noEvents();
    e.timeSinceLastActionMs = mesh::config::kSleepTimeoutMs;
    e.isCharging = false;
    e.singleClick = true;
    TEST_ASSERT_EQUAL(State::EnteringSleep, nextState(State::Idle, e));
}

void test_transmit_beats_receive() {
    auto e = noEvents();
    e.singleClick = true;
    e.rxReady = true;
    TEST_ASSERT_EQUAL(State::Transmitting, nextState(State::Idle, e));
}

void test_hold_beats_receive() {
    auto e = noEvents();
    e.holdComplete = true;
    e.rxReady = true;
    TEST_ASSERT_EQUAL(State::Transmitting, nextState(State::Idle, e));
}

// ── Non-Idle states don't transition ────────────────────────────────────────

void test_transmitting_stays_transmitting() {
    auto e = noEvents();
    e.longPress = true;  // would normally cause PoweringOff
    TEST_ASSERT_EQUAL(State::Transmitting, nextState(State::Transmitting, e));
}

void test_receiving_stays_receiving() {
    auto e = noEvents();
    e.singleClick = true;
    TEST_ASSERT_EQUAL(State::Receiving, nextState(State::Receiving, e));
}

void test_entering_sleep_stays() {
    auto e = noEvents();
    e.longPress = true;
    TEST_ASSERT_EQUAL(State::EnteringSleep, nextState(State::EnteringSleep, e));
}

void test_powering_off_stays() {
    TEST_ASSERT_EQUAL(State::PoweringOff, nextState(State::PoweringOff, noEvents()));
}

// ── Edge cases ──────────────────────────────────────────────────────────────

void test_all_events_simultaneously() {
    auto e = noEvents();
    e.singleClick = true;
    e.doubleClick = true;
    e.holdComplete = true;
    e.longPress = true;
    e.rxReady = true;
    e.timeSinceLastActionMs = mesh::config::kSleepTimeoutMs;
    e.isCharging = false;
    // longPress has highest priority
    TEST_ASSERT_EQUAL(State::PoweringOff, nextState(State::Idle, e));
}

void test_exact_sleep_timeout_boundary() {
    auto e = noEvents();
    e.timeSinceLastActionMs = mesh::config::kSleepTimeoutMs;  // exactly equal
    e.isCharging = false;
    TEST_ASSERT_EQUAL(State::EnteringSleep, nextState(State::Idle, e));
}

void test_sleep_triggers_regardless_of_external_state() {
    // stayAwake is handled in main.cpp (handleSleep chooses path),
    // not in the state machine — it always returns EnteringSleep on timeout.
    auto e = noEvents();
    e.timeSinceLastActionMs = mesh::config::kSleepTimeoutMs;
    e.isCharging = false;
    TEST_ASSERT_EQUAL(State::EnteringSleep, nextState(State::Idle, e));
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_idle_stays_idle_with_no_events);
    RUN_TEST(test_single_click_transmits);
    RUN_TEST(test_hold_complete_transmits);
    RUN_TEST(test_long_press_powers_off);
    RUN_TEST(test_rx_ready_receives);
    RUN_TEST(test_double_click_stays_idle);

    RUN_TEST(test_sleep_timeout_when_not_charging);
    RUN_TEST(test_no_sleep_when_charging);
    RUN_TEST(test_no_sleep_before_timeout);

    RUN_TEST(test_power_off_beats_sleep);
    RUN_TEST(test_power_off_beats_transmit);
    RUN_TEST(test_sleep_beats_transmit);
    RUN_TEST(test_transmit_beats_receive);
    RUN_TEST(test_hold_beats_receive);

    RUN_TEST(test_transmitting_stays_transmitting);
    RUN_TEST(test_receiving_stays_receiving);
    RUN_TEST(test_entering_sleep_stays);
    RUN_TEST(test_powering_off_stays);

    RUN_TEST(test_all_events_simultaneously);
    RUN_TEST(test_exact_sleep_timeout_boundary);

    RUN_TEST(test_sleep_triggers_regardless_of_external_state);

    return UNITY_END();
}
