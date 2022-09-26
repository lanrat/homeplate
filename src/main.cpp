// Next 3 lines are a precaution, you can ignore those, and the example would also work without them
#ifndef ARDUINO_INKPLATE6PLUS
#error "Wrong board selection for this example, please select Inkplate 6 PLUS in the boards menu."
#endif

#include <driver/rtc_io.h> //ESP32 library used for deep sleep and RTC wake up pins
#include <rom/rtc.h> // Include ESP32 library for RTC (needed for rtc_get_reset_reason() function)
#include "homeplate.h"

#define USE_SDCARD false

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
    // reset GPIOs used for wake interupt
    rtc_gpio_deinit(GPIO_NUM_34);
    rtc_gpio_deinit(WAKE_BUTTON);

    // start inkplate display mutexes
    mutexI2C = xSemaphoreCreateMutex();
    mutexSPI = xSemaphoreCreateMutex();
    mutexDisplay = xSemaphoreCreateMutex();

    sleepBoot = (rtc_get_reset_reason(0) == DEEPSLEEP_RESET); // test for deep sleep wake

    // only run on fresh boot
    if (!sleepBoot)
        printChipInfo();

    // must be called before checkPads() so buttons can override pre-boot activity
    startActivity(DEFAULT_ACTIVITY);

    // check touchpads for wake event, must be done before display.begin()
    if (sleepBoot && TOUCHPAD_ENABLE)
    {
        Wire.begin(); // this is called again in display.begin(), but it does not appear to be an issue...
        checkBootPads();
    }

    // Take the mutex
    displayStart();
    i2cStart();
    display.begin(); // Init Inkplate library (you should call this function ONLY ONCE)
    display.rtcClearAlarmFlag(); // Clear alarm flag from any previous alarm
    // set which pads can allow wakeup
    display.setIntPinInternal(MCP23017_INT_ADDR, display.mcpRegsInt, PAD1, RISING);
    display.setIntPinInternal(MCP23017_INT_ADDR, display.mcpRegsInt, PAD2, RISING);
    display.setIntPinInternal(MCP23017_INT_ADDR, display.mcpRegsInt, PAD3, RISING);
    pinMode(WAKE_BUTTON, INPUT_PULLUP);

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

    Serial.println("[SETUP] starting time task");
    setupTimeAndSyncTask();

    Serial.println("[SETUP] starting WiFi task");
    wifiConnectTask();

    Serial.println("[SETUP] starting OTA task");
    startOTATask();

    Serial.println("[SETUP] starting MQTT task");
    startMQTTTask();

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
    vTaskDelay(3 * SECOND/portTICK_PERIOD_MS);
    lowBatteryCheck();
}
