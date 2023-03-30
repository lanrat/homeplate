#include <NTPClient.h>
#include <Timezone.h>
#include <ESP32Time.h>

#include "homeplate.h"
#include "timezone_config.h"
#define NTP_TASK_PRIORITY 3

bool ntpSynced = false;

ESP32Time rtc;

time_t tzOffset() {
    time_t epoch = 0;
    TimeChangeRule *tcr; // pointer to the time change rule, use to get TZ abbrev
    time_t local = tz.toLocal(epoch, &tcr);
    return local;
}

bool getNTPSynced()
{
    return ntpSynced;
}

void ntpSync(void *parameter)
{
    Serial.printf("[TIME] Timezone offset: %lu\n", rtc.offset);

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
        display.rtcSetTime(hour(et), minute(et), second(et));
        display.rtcSetDate(weekday(et), day(et), month(et), year(et));
        rtc.setTime(et);
        i2cEnd();
        ntpSynced = true;
        Serial.printf("[TIME] NTP sync local UNIX time (%u) %s \n", et, fullDateString().c_str());


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
    rtc.offset = tzOffset();
    i2cStart();
    unsigned long t = rtc.getEpoch();
    bool rtcSet = display.rtcIsSet();
    i2cEnd();
    Serial.printf("[TIME] RTC local time (%lu) %s\n", t, fullDateString().c_str());

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
