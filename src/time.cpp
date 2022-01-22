#include <NTPClient.h>
#include <TimeLib.h>
#include <Timezone.h>

#include "homeplate.h"
#define NTP_TASK_PRIORITY 3

// US Pacific Time Zone (Los Angeles)
TimeChangeRule myDST = {"PDT", Second, Sun, Mar, 2, (-7 * 60)}; // Daylight time = UTC - 7 hours
TimeChangeRule mySTD = {"PST", First, Sun, Nov, 2, (-8 * 60)};  // Standard time = UTC - 8 hours
Timezone usPT(myDST, mySTD);

bool ntpSynced = false;

bool getNTPSynced()
{
    return ntpSynced;
}

void ntpSync(void *parameter)
{
    WiFiUDP ntpUDP;
    NTPClient timeClient(ntpUDP, NTP_SERVER);
    while (true)
    {
        waitForWiFi();
        Serial.println("[TIME] Syncing RTC to NTP");
        timeClient.begin();
        if (!timeClient.forceUpdate())
        {
            Serial.printf("[TIME] NTP Sync failed\n");
            delay(30 * SECOND);
            continue;
        }
        timeClient.end();

        /* RTC */
        i2cStart();
        time_t et = timeClient.getEpochTime();
        display.rtcSetTime(hour(et), minute(et), second(et));
        display.rtcSetDate(weekday(et), day(et), month(et), year(et));
        setTime(display.rtcGetEpoch());
        i2cEnd();
        ntpSynced = true;
        Serial.printf("[TIME] RTC local time (%ld) %s\n", et, fullDateString());
        break;
    }
    printDebugStackSpace();
    vTaskDelete(NULL); // end self task
}

void setupTimeAndSyncTask()
{
    i2cStart();
    uint32_t t = display.rtcGetEpoch();
    setTime(t);
    bool rtcSet = display.rtcIsSet();
    i2cEnd();
    Serial.printf("[TIME] RTC local time (%u) %s\n", t, fullDateString());
    // Sync RTC if unset or fresh boot
    if (!rtcSet || !sleepBoot)
    {
        xTaskCreate(
            ntpSync,           /* Task function. */
            "NTP_TASK",        /* String with name of task. */
            2048,              /* Stack size */
            NULL,              /* Parameter passed as input of the task */
            NTP_TASK_PRIORITY, /* Priority of the task. */
            NULL);             /* Task handle. */
    }
}

char dateStringBuf[17]; // 1990-12-27 13:37
char *fullDateString()
{
    TimeChangeRule *tcr; //pointer to the time change rule, use to get TZ abbrev
    time_t utc = now();
    time_t local = usPT.toLocal(utc, &tcr);

    snprintf(dateStringBuf, 17, "%d-%02d-%02d %02d:%02d", year(local), month(local), day(local), hour(local), minute(local));
    return dateStringBuf;
}

char timeStringBuf[6]; // 13:37
char *timeString()
{
    TimeChangeRule *tcr; //pointer to the time change rule, use to get TZ abbrev
    time_t utc = now();
    time_t local = usPT.toLocal(utc, &tcr);

    snprintf(timeStringBuf, 17, "%02d:%02d", hour(local), minute(local));
    return timeStringBuf;
}