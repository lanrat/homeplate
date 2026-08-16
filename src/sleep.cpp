#include "homeplate.h"

#define uS_TO_S_FACTOR 1000000ULL // Conversion factor for micro seconds to seconds
#define SLEEP_TASK_PRIORITY 1
#define TOUCHPAD_WAKE_MASK (int64_t(1) << GPIO_NUM_34)

static unsigned long sleepTime;
uint32_t sleepDuration = 0; // set from plateCfg at boot
uint32_t sleepRefresh = 0;

void setSleepDuration(uint32_t sec)
{
    if (sec > 0 && sec < MAX_REFRESH_SEC)
      {
        sleepDuration = sec;
      }
      else
      {
        Serial.printf("[SLEEP][ERROR] refresh value is out of range: %d\n", sec);
      }
}

uint32_t getSleepDuration()
{
    return sleepDuration;
}

void gotoSleepNow()
{
    Serial.println("[SLEEP] prepping for sleep");
    if (sleepRefresh > 0)
    {
        Serial.printf("[SLEEP] overriding sleep %d with %d\n", sleepDuration, sleepRefresh);
        sleepDuration = sleepRefresh;
        sleepRefresh = 0;
    }

    i2cStart();
    // disconnect WiFi as it's no longer needed
    mqttStopTask(); // prevent i2c lock in main thread
    wifiStopTask(); // prevent i2c lock in main thread
    i2cEnd();

    // Prevent integer overflow by checking max sleep duration (ESP32 limit is ~71 minutes)
    const uint32_t MAX_SLEEP_SECONDS = 4200; // ~70 minutes to stay well under ESP32 limit
    uint32_t safeSleepDuration = (sleepDuration > MAX_SLEEP_SECONDS) ? MAX_SLEEP_SECONDS : sleepDuration;
    
    uint64_t sleepMicroseconds = (uint64_t)safeSleepDuration * uS_TO_S_FACTOR;
    if (esp_sleep_enable_timer_wakeup(sleepMicroseconds) != ESP_OK) {
        Serial.printf("[SLEEP] ERROR esp_sleep_enable_timer_wakeup(%llu) invalid value\n", sleepMicroseconds);
    }

    #ifdef WAKE_BUTTON
        // Enable wakeup from deep sleep on WAKE BUTTON
        esp_sleep_enable_ext0_wakeup(WAKE_BUTTON, LOW);
    #endif

    #if defined(ARDUINO_INKPLATE10) || defined(ARDUINO_INKPLATE10V2) || defined(ARDUINO_INKPLATE6) || defined(ARDUINO_INKPLATE6V2)
        // enable wake from MCP port expander
        if (TOUCHPAD_ENABLE)
            esp_sleep_enable_ext1_wakeup(TOUCHPAD_WAKE_MASK, ESP_EXT1_WAKEUP_ANY_HIGH);
    #endif

    #if TOUCHPAD_ENABLE && defined(HAS_TOUCHPADS)
        // Clear any latched MCP interrupt before sleeping. INTF can be set
        // by transient capacitive noise, by the act of enabling GPINTEN
        // while a pin happens to be HIGH, or by a real touch during sleep
        // prep. If left latched the MCP's INT line stays asserted, GPIO 34
        // stays HIGH, and the ESP32's ext1 wakeup fires immediately upon
        // esp_deep_sleep_start() — manifesting as a spurious "touchpad"
        // wake with no actual pad pressed. Reading INTCAP releases the
        // latch and de-asserts INT; a genuine subsequent press will then
        // properly re-trigger the wake.
        i2cStart();
        readMCPRegister(MCP23017_INTCAPA);
        readMCPRegister(MCP23017_INTCAPB);
        i2cEnd();
    #endif

    Serial.printf("[SLEEP] entering sleep for %u seconds (%u min)\n\n\n", safeSleepDuration, safeSleepDuration / 60);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    esp_deep_sleep_start(); // Put ESP32 into deep sleep. Program stops here.
}

// Wraparound-safe "has the deadline passed?". millis() rolls over every ~49
// days; before always-on the device rebooted every sleep cycle so it could
// never get there, but an externally-powered device now can. Casting the
// unsigned difference to int32_t treats it as a signed delta — positive means
// the deadline is behind us. Same approach as the wake lock expiry check.
static bool deadlinePassed(unsigned long deadline)
{
    return (int32_t)(millis() - deadline) >= 0;
}

void delaySleep(uint seconds)
{
    unsigned long now = millis();
    int32_t timeLeft = (int32_t)(sleepTime - now);
    // if the bumped time is farther in the future than our current sleep time
    int32_t ms = (int32_t)(seconds * SECOND);
    if (ms > timeLeft)
    {
        Serial.printf("[SLEEP] delaying sleep for %u seconds\n", seconds);
        sleepTime = now + ms;
    }
}

void checkSleep(void *parameter)
{
    while (true)
    {
        printDebug("[SLEEP] sleep loop..");
        // check the sleep time
        while (!deadlinePassed(sleepTime))
        {
            vTaskDelay(SECOND / portTICK_PERIOD_MS);
        }

        // wait for any wake locks to release
        if (anyWakeLocksHeld())
        {
            vTaskDelay(SECOND / portTICK_PERIOD_MS);
            continue;
        }

        if (plateCfg.alwaysOn)
        {
            // Never deep-sleep. Re-run the default activity on the cadence the
            // sleep duration would have used, and leave WiFi and MQTT up so
            // commands from Home Assistant apply immediately instead of at the
            // next wake — which is the whole point of the mode.
            waitForOTA();
            uint32_t period = getSleepDuration();
            if (period == 0)
            {
                period = (uint32_t)plateCfg.sleepMinutes * 60;
            }
            // Floor the cadence. A zero period would re-arm the deadline to
            // "now" and spin this task, and anything under
            // MIN_ACTIVITY_RESTART_SECS is silently swallowed by the debounce
            // in runActivities — so a short MQTT `refresh` would look like it
            // did nothing at all. Deep sleep never needed this: the ESP32
            // timer takes any value and every wake is a fresh boot.
            if (period < ALWAYS_ON_MIN_PERIOD_SEC)
            {
                period = ALWAYS_ON_MIN_PERIOD_SEC;
            }
            sleepTime = millis() + ((unsigned long)period * SECOND);
            // Heap numbers because this is the one build that runs for weeks:
            // every render ps_malloc()s a full-frame buffer, and fragmentation
            // that a nightly reboot used to paper over now has to be watched.
            Serial.printf("[SLEEP] always-on: refreshing now, next in %u seconds (%u min), up %lus, heap %u free / %u largest\n",
                          period, period / 60, millis() / SECOND,
                          ESP.getFreeHeap(), ESP.getMaxAllocHeap());
            // force: startActivity()'s 60-second debounce assumes the default
            // activity is only re-queued by a button or an MQTT command. Here
            // the timer is authoritative, and at sleepMinutes=1 the debounce
            // would swallow the refresh outright. A deep-sleep wake dodges it
            // only because the statics it tests reset across the reboot.
            startActivity(activityFromString(plateCfg.defaultActivityStr), true);
            continue;
        }

        // wait for mqtt messages to send
        // only check if both wifi and mqtt did not fail
        if (!getWifIFailed() && !getMQTTFailed() && mqttRunning())
        {
            Serial.printf("[SLEEP] waiting on MQTT..\n");
            vTaskDelay(2 * SECOND / portTICK_PERIOD_MS);
            continue; // reset waiting
        }

        startActivity(NONE);
        waitForOTA(); // dont sleep if there is an OTA being performed
        printDebugStackSpace();
        // i2cStart();
        // displayStart();
        // display.einkOff();
        // displayEnd();
        // i2cEnd();
        gotoSleepNow();
    }
}

void sleepTask()
{
    // SLEEP_TIMEOUT_SEC is a floor on how long to stay up before deep-sleeping,
    // which has no meaning when we never sleep: the first refresh is the one
    // setup() already queued. Start the always-on timer a full period out, or
    // it fires 15 seconds after boot and immediately re-renders what the boot
    // activity just drew.
    uint32_t initialSec = plateCfg.alwaysOn ? (uint32_t)plateCfg.sleepMinutes * 60 : SLEEP_TIMEOUT_SEC;
    sleepTime = ((unsigned long)initialSec * SECOND) + millis();

    xTaskCreate(
        checkSleep,
        "SLEEP_TASK",        // Task name
        // 4096 under arduino-esp32 v3 — eventually calls WiFi.disconnect()
        // via wifiStopTask() which has fat lwIP/WiFi call chains.
        4096,                // Stack size
        NULL,                // Parameter
        SLEEP_TASK_PRIORITY, // Task priority
        NULL                 // Task handle
    );
}
