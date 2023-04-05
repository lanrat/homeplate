#include <NTPClient.h>
#include <Timezone.h>
#include <ESP32Time.h>

#include "homeplate.h"
#include "timezone_config.h"
#define NTP_TASK_PRIORITY 3

bool ntpSynced = false;

ESP32Time rtc;

long tzOffset(time_t epoch) {
    TimeChangeRule *tcr; // pointer to the time change rule, use to get TZ abbrev
    time_t local = tz.toLocal(epoch, &tcr);
    return local - epoch;
}

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
        printDebug("[TIME] loop...");
        waitForWiFi();
        Serial.println("[TIME] Syncing RTC to NTP");
        timeClient.begin();
        if (!timeClient.forceUpdate())
        {
            Serial.printf("[TIME] NTP Sync failed\n");
            vTaskDelay((30 * SECOND) / portTICK_PERIOD_MS);
            continue;
        }
        timeClient.end();

        /* RTC */
        i2cStart();
        time_t et = timeClient.getEpochTime();
        display.rtcSetEpoch(et);
        rtc.setTime(et);
        i2cEnd();

        ntpSynced = true;
        // rtc.offset is unsigned, but it is used as a signed long for negative offsets
        long offset = tzOffset(et);
        rtc.offset = offset;
        unsigned long localTime = rtc.getEpoch();
        Serial.printf("[TIME] NTP UNIX time Epoch(%u)\n", et);
        Serial.printf("[TIME] Timezone offset: (%ld) %ld hours\n", offset, (offset/60/60));
        Serial.printf("[TIME] synced local UNIX time Epoch(%u) %s \n", localTime, fullDateString().c_str());

        i2cStart();
        bool rtcSet = display.rtcIsSet();
        i2cEnd();
        if (!rtcSet) {
            Serial.printf("[TIME] ERROR: Failed to set RTC!\n");
        }

        break;
    }
    printDebugStackSpace();
    vTaskDelete(NULL); // end self task
}

void setupTimeAndSyncTask()
{
    i2cStart();
    bool rtcSet = display.rtcIsSet();
    if (rtcSet) {
        uint32_t rtcEpoch = display.rtcGetEpoch();
        rtc.offset = tzOffset(rtcEpoch);
    }
    unsigned long t = rtc.getLocalEpoch();
    i2cEnd();
    Serial.printf("[TIME] local time (%lu) %s\n", t, fullDateString().c_str());

    if (rtcSet && t < 1577885820) {
        Serial.printf("[TIME] ERROR: RTC time is too far in past. RTC likely has wrong value!\n");
    }

    #ifdef NTP_SERVER
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
    #endif
}

String fullDateString() {
    return rtc.getTimeDate(true);
}

String timeString() {
    return rtc.getTime("%H:%M");
}
