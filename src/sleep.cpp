#include "homeplate.h"

#define uS_TO_S_FACTOR 1000000ULL // Conversion factor for micro seconds to seconds
#define SLEEP_TASK_PRIORITY 1
#define TOUCHPAD_WAKE_MASK (int64_t(1) << GPIO_NUM_34)

static unsigned long sleepTime;
uint32_t sleepDuration = TIME_TO_SLEEP_SEC;
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

    #if defined(ARDUINO_INKPLATE10)
        // set MCP interrupts
        if (TOUCHPAD_ENABLE)
            display.setIntOutput(1, false, false, HIGH, IO_INT_ADDR);
        #endif
    i2cEnd();


    // Go to sleep for TIME_TO_SLEEP seconds
    if (esp_sleep_enable_timer_wakeup(sleepDuration * uS_TO_S_FACTOR) != ESP_OK) {
        Serial.printf("[SLEEP] ERROR esp_sleep_enable_timer_wakeup(%u) invalid value", sleepDuration * uS_TO_S_FACTOR);
    }

    // Enable wakeup from deep sleep on gpio 36 (WAKE BUTTON)
    esp_sleep_enable_ext0_wakeup(WAKE_BUTTON, LOW);
    #if defined(ARDUINO_INKPLATE10)
        // enable wake from MCP port expander
        if (TOUCHPAD_ENABLE)
            esp_sleep_enable_ext1_wakeup(TOUCHPAD_WAKE_MASK, ESP_EXT1_WAKEUP_ANY_HIGH);
    #endif
    Serial.printf("[SLEEP] entering sleep for %u seconds (%u min)\n\n\n", sleepDuration, sleepDuration / 60);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    esp_deep_sleep_start(); // Put ESP32 into deep sleep. Program stops here.
}

void delaySleep(uint seconds)
{
    unsigned long timeLeft = sleepTime - millis();
    // if the bumped time is farther in the future than our current sleep time
    int ms = seconds * SECOND;
    if (ms > timeLeft)
    {
        Serial.printf("[SLEEP] delaying sleep for %u seconds\n", seconds);
        sleepTime = ms + millis();
    }
}

void checkSleep(void *parameter)
{
    while (true)
    {
        printDebug("[SLEEP] sleep loop..");
        // check the sleep time
        while (sleepTime > millis())
        {
            vTaskDelay(SECOND / portTICK_PERIOD_MS);
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
    sleepTime = (SLEEP_TIMEOUT_SEC * SECOND) + millis();

    xTaskCreate(
        checkSleep,
        "SLEEP_TASK",        // Task name
        2048,                // Stack size
        NULL,                // Parameter
        SLEEP_TASK_PRIORITY, // Task priority
        NULL                 // Task handle
    );
}
