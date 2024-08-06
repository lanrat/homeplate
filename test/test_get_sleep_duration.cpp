#include <unity.h>
#include "sleep_duration.h"

SleepScheduleSlot testSlots[] = {
    {
        .start_dow = 1,
        .start_hour = 0,
        .start_minute = 0,
        .end_dow = 5,
        .end_hour = 8,
        .end_minute = 30,
        .sleep_in_seconds = 10,
    },
    {
        .start_dow = 1,
        .start_hour = 8,
        .start_minute = 30,
        .end_dow = 5,
        .end_hour = 24,
        .end_minute = 0,
        .sleep_in_seconds = 20,
    },
    {
        .start_dow = 6,
        .start_hour = 0,
        .start_minute = 0,
        .end_dow = 7,
        .end_hour = 9,
        .end_minute = 30,
        .sleep_in_seconds = 30,
    },
    {
        .start_dow = 6,
        .start_hour = 9,
        .start_minute = 30,
        .end_dow = 7,
        .end_hour = 24,
        .end_minute = 0,
        .sleep_in_seconds = 40,
    },
};

const size_t testSlotCount = sizeof(testSlots) / sizeof(testSlots[0]);

SleepDefaults defaults = {
    .normalSleep = 50,
    .quickSleep = 60,
};

void setUp(void) {
    // set stuff up here
}

void tearDown(void) {
    // clean stuff up here
}

void test_tuesday_morning(void) {
    TimeInfo time = {
        .dow = 2,
        .hour = 9,
        .minute = 0
    };

    uint sleep = getSleepDuration(testSlots, testSlotCount, time, defaults, false);
    TEST_ASSERT_EQUAL(20, sleep);
}

void test_tuesday_morning_quick(void) {
    TimeInfo time = {
        .dow = 2,
        .hour = 9,
        .minute = 0
    };

    uint sleep = getSleepDuration(testSlots, testSlotCount, time, defaults, true);
    TEST_ASSERT_EQUAL(60, sleep);
}

void test_tuesday_night(void) {
    TimeInfo time = {
        .dow = 2,
        .hour = 1,
        .minute = 30
    };

    uint sleep = getSleepDuration(testSlots, testSlotCount, time, defaults, false);
    TEST_ASSERT_EQUAL(10, sleep);
}

void test_saturday_morning(void) {
    TimeInfo time = {
        .dow = 6,
        .hour = 9,
        .minute = 0
    };

    uint sleep = getSleepDuration(testSlots, testSlotCount, time, defaults, false);
    TEST_ASSERT_EQUAL(30, sleep);
}

void test_saturday_morning_quick(void) {
    TimeInfo time = {
        .dow = 6,
        .hour = 9,
        .minute = 0
    };

    uint sleep = getSleepDuration(testSlots, testSlotCount, time, defaults, true);
    TEST_ASSERT_EQUAL(60, sleep);
}

void test_saturday_night(void) {
    TimeInfo time = {
        .dow = 6,
        .hour = 1,
        .minute = 30
    };

    uint sleep = getSleepDuration(testSlots, testSlotCount, time, defaults, false);
    TEST_ASSERT_EQUAL(30, sleep);
}

void test_unconfigured_block(void) {
    TimeInfo time = {
        .dow = 10,
        .hour = 1,
        .minute = 30
    };

    uint sleep = getSleepDuration(testSlots, testSlotCount, time, defaults, false);
    TEST_ASSERT_EQUAL(50, sleep);
}

void test_unset_rtc(void) {
    TimeInfo time = {
        .dow = -1,
        .hour = -1,
        .minute = -1
    };

    uint sleep = getSleepDuration(testSlots, testSlotCount, time, defaults, false);
    TEST_ASSERT_EQUAL(50, sleep);
}

int main( int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_tuesday_morning);
    RUN_TEST(test_tuesday_morning_quick);
    RUN_TEST(test_tuesday_night);
    RUN_TEST(test_saturday_morning);
    RUN_TEST(test_saturday_morning_quick);
    RUN_TEST(test_saturday_night);
    RUN_TEST(test_unconfigured_block);
    RUN_TEST(test_unset_rtc);

    UNITY_END();
}
