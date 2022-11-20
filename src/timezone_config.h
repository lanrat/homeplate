#pragma once
#include "config.h"
#include <Timezone.h>

#if defined TIMEZONE_UTC
    // UTC
    TimeChangeRule utcRule = {"UTC", Last, Sun, Mar, 1, 0};     // UTC
    Timezone tz(utcRule);

#elif defined TIMEZONE_ausET
    // Australia Eastern Time Zone (Sydney, Melbourne)
    TimeChangeRule aEDT = {"AEDT", First, Sun, Oct, 2, 660};    // UTC + 11 hours
    TimeChangeRule aEST = {"AEST", First, Sun, Apr, 3, 600};    // UTC + 10 hours
    Timezone tz(aEDT, aEST);

#elif defined TIMEZONE_MSK
    // Moscow Standard Time (MSK, does not observe DST)
    TimeChangeRule msk = {"MSK", Last, Sun, Mar, 1, 180};
    Timezone tz(msk);

#elif defined TIMEZONE_CE
    // Central European Time (Frankfurt, Paris)
    TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};     // Central European Summer Time
    TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};       // Central European Standard Time
    Timezone tz(CEST, CET);

#elif defined TIMEZONE_UK
    // United Kingdom (London, Belfast)
    TimeChangeRule BST = {"BST", Last, Sun, Mar, 1, 60};        // British Summer Time
    TimeChangeRule GMT = {"GMT", Last, Sun, Oct, 2, 0};         // Standard Time
    Timezone tz(BST, GMT);

#elif defined TIMEZONE_usET
    // US Eastern Time Zone (New York, Detroit)
    TimeChangeRule usEDT = {"EDT", Second, Sun, Mar, 2, -240};  // Eastern Daylight Time = UTC - 4 hours
    TimeChangeRule usEST = {"EST", First, Sun, Nov, 2, -300};   // Eastern Standard Time = UTC - 5 hours
    Timezone tz(usEDT, usEST);

#elif defined TIMEZONE_usCT
    // US Central Time Zone (Chicago, Houston)
    TimeChangeRule usCDT = {"CDT", Second, Sun, Mar, 2, -300};
    TimeChangeRule usCST = {"CST", First, Sun, Nov, 2, -360};
    Timezone tz(usCDT, usCST);

#elif defined TIMEZONE_ausET
    // US Mountain Time Zone (Denver, Salt Lake City)
    TimeChangeRule usMDT = {"MDT", Second, Sun, Mar, 2, -360};
    TimeChangeRule usMST = {"MST", First, Sun, Nov, 2, -420};
    Timezone tz(usMDT, usMST);

#elif defined TIMEZONE_usAZ
    // Arizona is US Mountain Time Zone but does not use DST
    TimeChangeRule usMST = {"MST", First, Sun, Nov, 2, -420};
    Timezone tz(usMST);

#elif defined TIMEZONE_usPT
    // US Pacific Time Zone (Las Vegas, Los Angeles)
    TimeChangeRule usPDT = {"PDT", Second, Sun, Mar, 2, -420};  // Daylight time = UTC - 7 hours
    TimeChangeRule usPST = {"PST", First, Sun, Nov, 2, -480};  // Standard time = UTC - 8 hours
    Timezone tz(usPDT, usPST);

#elif defined TIMEZONE_ausET
    // New Zealand Time Zone
    TimeChangeRule nzSTD = {"NZST", First, Sun, Apr, 3, 720};   // UTC + 12 hours
    TimeChangeRule nzDST = {"NZDT", Last, Sun, Sep, 2, 780};    // UTC + 13 hours
    Timezone tz(nzDST, nzSTD);

#else
    #error Unknown TIMEZONE!
#endif
