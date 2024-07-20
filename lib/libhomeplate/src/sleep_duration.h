#ifndef UTIL_SLEEP_DURATION_H
#define UTIL_SLEEP_DURATION_H
#include <Arduino.h>

struct SleepTimeBlock {
    int start_dow; // 1-7, 1=monday
    int start_hour; // 0-23
    int start_minute; // 0-59
    int end_dow;
    int end_hour;
    int end_minute;
    uint sleep_in_seconds;
};

struct TimeInfo {
    uint dow;
    uint hour;
    uint minute;
};

struct SleepDefaults {
    uint normalSleep;
    uint quickSleep;
};

uint getSleepDuration(SleepTimeBlock sleepTimeBlocks[], size_t size, TimeInfo time, SleepDefaults sleep, bool doQuickSleep = false);

#endif
