#include <NTPClient.h>
#include <Timezone.h>
#include <ESP32Time.h>

#include "homeplate.h"
#include "timezone_config.h"
#define NTP_TASK_PRIORITY 3

#define JAN_1_2000 946684800

bool ntpSynced = false;
bool rtcSet = false;

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

#ifdef NTP_SERVER
void ntpSync(void *parameter)
{
    WiFiUDP ntpUDP;
    NTPClient timeClient(ntpUDP, NTP_SERVER);
    const int MAX_NTP_RETRIES = 5;
    int retryCount = 0;
    while (retryCount < MAX_NTP_RETRIES)
    {
        printDebug("[TIME] loop...");
        waitForWiFi();
        Serial.printf("[TIME] Syncing RTC to NTP (attempt %d/%d)\n", retryCount + 1, MAX_NTP_RETRIES);
        timeClient.begin();
        if (!timeClient.forceUpdate())
        {
            retryCount++;
            Serial.printf("[TIME] NTP Sync failed\n");
            if (retryCount >= MAX_NTP_RETRIES)
            {
                Serial.printf("[TIME] NTP Sync failed after %d attempts, giving up\n", MAX_NTP_RETRIES);
                break;
            }
            vTaskDelay((30 * SECOND) / portTICK_PERIOD_MS);
            continue;
        }
        timeClient.end();

        // get current times for comparison after sync
        unsigned long localTime = rtc.getLocalEpoch();

        /* RTC */
        i2cStart();
        uint32_t rtcEpoch = display.rtcGetEpoch();
        time_t ntp_et = timeClient.getEpochTime();
        display.rtcSetEpoch(ntp_et);
        rtc.setTime(ntp_et);
        i2cEnd();

        // print how far the clocks are off
        long delta_s = (ntp_et - localTime);
        Serial.printf("[TIME] Internal clock was adjusted by %ld seconds\n", delta_s);
        Serial.printf("[TIME] Internal RTC was adjusted by %ld seconds\n", (ntp_et - rtcEpoch));

        ntpSynced = true;
        // rtc.offset is unsigned, but it is used as a signed long for negative offsets
        long offset = tzOffset(ntp_et);
        rtc.offset = offset;
        localTime = rtc.getLocalEpoch();
        Serial.printf("[TIME] NTP UNIX time Epoch(%ld)\n", ntp_et);
        Serial.printf("[TIME] Timezone offset: (%ld) %ld hours\n", offset, (offset/60/60));
        Serial.printf("[TIME] synced local UNIX time Epoch(%ld) %s \n", localTime, fullDateString().c_str());

        i2cStart();
        rtcSet = display.rtcIsSet();
        i2cEnd();
        if (!rtcSet) {
            Serial.printf("[TIME] ERROR: Failed to set RTC!\n");
        }

        break;
    }
    printDebugStackSpace();
    vTaskDelete(NULL); // end self task
}
#endif

void setupTimeAndSyncTask()
{
    unsigned long localTime = rtc.getLocalEpoch();
    i2cStart();
    rtcSet = display.rtcIsSet();
    if (rtcSet) {
        uint32_t rtcEpoch = display.rtcGetEpoch();
        rtc.offset = tzOffset(rtcEpoch);
        Serial.printf("[TIME] Internal Clock and RTC differ by %ld seconds. local(%ld) RTC(%ld)\n", (localTime - rtcEpoch), localTime, rtcEpoch);
    }
    i2cEnd();
    Serial.printf("[TIME] local time (%lu) %s\n", localTime, fullDateString().c_str());

    if (rtcSet && localTime < JAN_1_2000) {
        Serial.printf("[TIME] ERROR: RTC time is too far in past. RTC likely has wrong value!\n");
        rtcSet = false;
    }

    #ifdef NTP_SERVER
        // Sync RTC if unset or fresh boot
        bool resync = ((bootCount % (NTP_SYNC_INTERVAL)) == 0);
        if (resync) Serial.printf("[TIME] re-syncing NTP: on boot %d, ever %d\n", bootCount, NTP_SYNC_INTERVAL);
        if (!rtcSet || !sleepBoot || resync)
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

int getDayOfWeek(bool weekStartsOnMonday) {
    if (!rtcSet) {
        // return -1 as long as rtc is not set
        return -1;
    }

    int dow = rtc.getDayofWeek();
    // rtc.getDayofWeek() returns 0-6 where 0=Sunday
    // We need to return 1-7
    if (weekStartsOnMonday) {
        // Monday=1, Tuesday=2, ..., Sunday=7
        return (dow == 0) ? 7 : dow;
    } else {
        // Sunday=1, Monday=2, ..., Saturday=7
        return dow + 1;
    }
}

int getHour() {
    if (!rtcSet) {
        return -1;
    }
    return rtc.getHour(true);
}

int getMinute() {
    if (!rtcSet) {
        return -1;
    }
    return rtc.getMinute();
}