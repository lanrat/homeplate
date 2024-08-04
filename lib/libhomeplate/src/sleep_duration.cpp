#include "sleep_duration.h"

uint getSleepDuration(SleepScheduleSlot sleepScheduleSlots[], size_t size, TimeInfo time, SleepDefaults defaults, bool doQuickSleep)
{
    if (doQuickSleep) {
        return defaults.quickSleep;
    }

    uint timeInMinutes = time.hour * 60 + time.minute;

    for (int i = 0; i < size; i++) {
        SleepScheduleSlot b = sleepScheduleSlots[i];

        if (time.dow >= b.start_dow && time.dow <= b.end_dow) {
            uint startTimeInMinutes = b.start_hour * 60 + b.start_minute;
            uint endTimeInMinutes = b.end_hour * 60 + b.end_minute;

            if (timeInMinutes >= startTimeInMinutes && timeInMinutes < endTimeInMinutes) {
                return b.sleep_in_seconds;
            }
        }
    }

    return defaults.normalSleep;
}
