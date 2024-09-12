// Note:
//   This file is only needed when configuring a custom sleep schedule.
//   If using a single/static sleep duration then this file can be omitted
/* REMOVE THIS LINE TO ENABLE THE SLEEP SCHEDULE
#include "config.h"

// How long to sleep between image refreshes
// - there is no validation of any kind, make sure your slots are continuous
// - falls back to TIME_TO_SLEEP_MIN if your slots are not continuous
// - a schedule slot is configured per day, i.e. cronjob style
// - dow = DayOfWeek, starts at 1 = Monday to 7 = Sunday
// SleepScheduleSlot sleepSchedule[] = {
    // EXAMPLE BLOCKS
    { // On each workday from 00:00 to 08:30 sleep for 1 hour
        .start_dow = 1,
        .start_hour = 0,
        .start_minute = 0,
        .end_dow = 5,
        .end_hour = 8,
        .end_minute = 30,
        .sleep_in_seconds = 3600,
    },
    { // On each workday from 08:30 to 22:00 sleep for 5 minutes
        .start_dow = 1,
        .start_hour = 8,
        .start_minute = 30,
        .end_dow = 5,
        .end_hour = 22,
        .end_minute = 0,
        .sleep_in_seconds = 300,
    },
    { // On each workday from 22:00 to 24:00 sleep for 30 minutes
        .start_dow = 1,
        .start_hour = 8,
        .start_minute = 30,
        .end_dow = 5,
        .end_hour = 24,
        .end_minute = 0,
        .sleep_in_seconds = 1800,
    },
    { // On saturday & sunday from 00:00 to 09:30 sleep for 1 hour
        .start_dow = 6,
        .start_hour = 0,
        .start_minute = 0,
        .end_dow = 7,
        .end_hour = 9,
        .end_minute = 30,
        .sleep_in_seconds = 3600,
    },
    { // On saturday & sunday from 09:30 to 24:00 sleep for 5 minutes
        .start_dow = 6,
        .start_hour = 9,
        .start_minute = 30,
        .end_dow = 7,
        .end_hour = 24,
        .end_minute = 0,
        .sleep_in_seconds = 300,
    },
};

const size_t sleepScheduleSize = sizeof(sleepSchedule) / sizeof(sleepSchedule[0]);
/**/
