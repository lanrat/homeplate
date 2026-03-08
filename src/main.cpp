// Ensure a supported board is selected
#if !defined(ARDUINO_INKPLATE10) && !defined(ARDUINO_INKPLATE10V2) && !defined(ARDUINO_ESP32_DEV) && !defined(ARDUINO_INKPLATE6V2) && !defined(ARDUINO_INKPLATE6PLUS) && !defined(ARDUINO_INKPLATE6PLUSV2) && !defined(ARDUINO_INKPLATE6FLICK)
#error "Unsupported board selection, please select a supported Inkplate board."
#endif

#include <driver/rtc_io.h> //ESP32 library used for deep sleep and RTC wake up pins
#include <rom/rtc.h>       // Include ESP32 library for RTC (needed for rtc_get_reset_reason() function)
#include "homeplate.h"

Inkplate display(INKPLATE_1BIT);
SemaphoreHandle_t mutexI2C, mutexSPI, mutexDisplay;

bool sleepBoot = false;

// Store int in rtc data, to remain persistent during deep sleep, reset on power up.
RTC_DATA_ATTR uint bootCount = 0;

void setup()
{
    Serial.begin(115200);
    Serial.printf("\n\n[SETUP] starting, version(%s) boot: %u\n", VERSION, bootCount);
    ++bootCount;
    // reset GPIOs used for wake interrupt
    #if defined(ARDUINO_INKPLATE10) || defined(ARDUINO_INKPLATE10V2) || defined(ARDUINO_ESP32_DEV) || defined(ARDUINO_INKPLATE6V2)
        rtc_gpio_deinit(GPIO_NUM_34); // touchpad wake mask pin
    #endif
    #ifdef WAKE_BUTTON
        rtc_gpio_deinit(WAKE_BUTTON);
    #endif

    // start inkplate display mutexes
    mutexI2C = xSemaphoreCreateMutex();
    mutexSPI = xSemaphoreCreateMutex();
    mutexDisplay = xSemaphoreCreateMutex();

    sleepBoot = (rtc_get_reset_reason(0) == DEEPSLEEP_RESET); // test for deep sleep wake

    // only run on fresh boot
    if (!sleepBoot)
        printChipInfo();

    // Load configuration from NVS (with compile-time defaults as fallback)
    loadConfig();

    // Set sleep duration from config (must happen before any sleep logic)
    setSleepDuration(plateCfg.sleepMinutes * 60);

    // must be called before checkPads() so buttons can override pre-boot activity
    startActivity(activityFromString(plateCfg.defaultActivityStr));

    // check touchpads for wake event, must be done before display.begin()
    if (sleepBoot && TOUCHPAD_ENABLE)
    {
        Wire.begin(); // this is called again in display.begin(), but it does not appear to be an issue...
        checkBootPads();
    }

    // Take the mutex
    displayStart();
    i2cStart();
    display.begin();             // Init Inkplate library (you should call this function ONLY ONCE)
    display.rtcClearAlarmFlag(); // Clear alarm flag from any previous alarm
    setupWakePins();

    // setup display
    if (sleepBoot)
        display.preloadScreen(); // copy saved screen state to buffer
    display.clearDisplay();      // Clear frame buffer of display
    i2cEnd();
    displayEnd();

    displayStatusMessage("Boot %d %s", bootCount, bootReason());

    // print battery state
    double voltage = 0;
    i2cStart();
    voltage = display.readBattery();
    i2cEnd();
    voltage = roundf(voltage * 100) / 100; // rounds to 2 decimal places
    int percent = getBatteryPercent(voltage);
    Serial.printf("[SETUP] Battery: %d%% (%.2fv)\n", percent, voltage);

    if (!sleepBoot)
        splashScreen();

    if (USE_SDCARD)
    {
        spiStart();
        if (display.sdCardInit())
        {
            Serial.println("[SETUP] SD card init OK");
        }
        else
        {
            Serial.println("[SETUP] SD card init FAILED");
        }
        spiEnd();
    }

    Serial.println("[SETUP] starting button task");
    startMonitoringButtonsTask();

    // WiFiManager: handles initial WiFi connection and config portal
    // Force portal if device has never been configured via NVS,
    // if the selected activity is missing required settings,
    // or if the wake button is held during boot
    bool forcePortal = !isConfigured();
#ifdef WAKE_BUTTON
    if (!forcePortal && digitalRead(WAKE_BUTTON) == LOW)
    {
        Serial.println("[SETUP] Wake button held at boot, forcing config portal");
        forcePortal = true;
    }
#endif
    if (!forcePortal)
    {
        Activity act = activityFromString(plateCfg.defaultActivityStr);
        if ((act == HomeAssistant && strlen(plateCfg.imageUrl) == 0) ||
            (act == Trmnl && strlen(plateCfg.trmnlId) == 0) ||
            (act == GuestWifi && strlen(plateCfg.qrWifiName) == 0))
        {
            Serial.println("[SETUP] Activity missing required settings, forcing config portal");
            forcePortal = true;
        }
    }
    if (forcePortal)
        Serial.println("[SETUP] Device not configured, forcing config portal");
    Serial.println("[SETUP] starting WiFiManager");
    if (!startWiFiManager(forcePortal))
    {
        // WiFiManager timed out - no connection established
        Serial.println("[SETUP] WiFiManager timeout, going to sleep");
        displayUnconfiguredScreen();
        setSleepDuration(plateCfg.sleepMinutes * 60);
        gotoSleepNow();
        return; // won't reach here after deep sleep
    }

    Serial.println("[SETUP] WiFi connected, continuing setup");

    Serial.println("[SETUP] starting time task");
    setupTimeAndSyncTask();

    // Start WiFi reconnection task for ongoing connectivity
    Serial.println("[SETUP] starting WiFi reconnect task");
    wifiConnectTask();

    if (plateCfg.enableOta)
    {
        Serial.println("[SETUP] starting OTA task");
        startOTATask();
    }

    if (strlen(plateCfg.mqttHost) > 0)
    {
        Serial.println("[SETUP] starting MQTT task");
        startMQTTTask();
    }

    Serial.println("[SETUP] starting sleep task");
    sleepTask();

    Serial.println("[SETUP] starting activities task");
    startActivitiesTask();
}

// if there is too much in here it can cause phantom touches...
void loop()
{
    // main loop runs at priority 1
    printDebug("[MAIN] loop...");
    vTaskDelay(3 * SECOND / portTICK_PERIOD_MS);
    lowBatteryCheck();
}
