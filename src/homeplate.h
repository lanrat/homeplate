#pragma once

#include <Inkplate.h>
#include "fonts/Roboto_12.h"
#include "fonts/Roboto_16.h"
#include "fonts/Roboto_32.h"
#include "fonts/Roboto_64.h"
#include "fonts/Roboto_128.h"
#include "config.h"

// check that config file is correctly set
#if !defined CONFIG_H
#error Missing config.h!
#error HINT: copy config_example.h to config.h and make changes.
#endif

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask,
                                          signed char *pcTaskName);

extern Inkplate display;
extern SemaphoreHandle_t mutexI2C, mutexDisplay, mutexSPI;
extern bool sleepBoot;
extern uint bootCount, activityCount;

#define i2cStart() xSemaphoreTake(mutexI2C, portMAX_DELAY)
#define i2cEnd() xSemaphoreGive(mutexI2C)
#define spiStart() xSemaphoreTake(mutexSPI, portMAX_DELAY)
#define spiEnd() xSemaphoreGive(mutexSPI)
#define displayStart() xSemaphoreTake(mutexDisplay, portMAX_DELAY)
#define displayEnd() xSemaphoreGive(mutexDisplay)

#define max(x, y) (((x) >= (y)) ? (x) : (y))

#define WAKE_BUTTON GPIO_NUM_36

#define VERSION __DATE__ ", " __TIME__

// Image "colors" (3bit mode)
#define C_BLACK 0
#define C_GREY_1 1
#define C_GREY_2 2
#define C_GREY_3 3
#define C_GREY_4 4
#define C_GREY_5 5
#define C_GREY_6 6
#define C_WHITE 7

// for second to ms conversions
#define SECOND 1000

// WiFi
void wifiConnectTask();
void wifiStopTask();
void waitForWiFi();
bool getWifIFailed();

// QR
void displayWiFiQR();

// info
void displayInfoScreen();

// Image
bool remotePNG(const char *);
bool drawPngFromBuffer(uint8_t *buff, int32_t len, int x, int y);
uint16_t centerTextX(const char *t, int16_t x1, int16_t x2, int16_t y, bool lock = true);
void displayStatusMessage(const char *format, ...);
void splashScreen();

// Input
void startMonitoringButtonsTask();
void checkBootPads();

// Sleep
#define TIME_TO_SLEEP_SEC (TIME_TO_SLEEP_MIN * 60)    // How long ESP32 will be in deep sleep (in seconds)
#define TIME_TO_QUICK_SLEEP_SEC 5 * 60 // 5 minutes. How long ESP32 will be in deep sleep (in seconds) for short activities
void startSleep();
void setSleepRefresh(uint32_t sec);
void setSleepDuration(uint32_t sec);
void gotoSleepNow();

// time
void setupTimeAndSyncTask();
bool getNTPSynced();
char *timeString();
char *fullDateString();

// MQTT
void startMQTTTask();
bool getMQTTFailed();
bool mqttConnected();
void mqttStopTask();
void waitForMQTT();
void sendMQTTStatus();
bool mqttRunning();
void sendMQTTStatus();
void startMQTTStatusTask();

// OTA
void startOTATask();
void waitForOTA();

// util
const char *bootReason();
uint getBatteryPercent(double voltage);
void printChipInfo();
void lowBatteryCheck();
void printDebugStackSpace();
void displayBatteryWarning();
void printDebug(const char *s);

// message
void setMessage(const char *m);
void displayMessage(const char * = NULL);
const char* getMessage();

// activity
enum Activity
{
    NONE,
    HomeAssistant,
    GuestWifi,
    Info,
    Message,
    IMG,
};

#define DEFAULT_ACTIVITY HomeAssistant
void startActivity(Activity activity);
void startActivitiesTask();
bool stopActivity();
void sleepTask();
void delaySleep(uint seconds);

/*
 * Global Settings
 */

// Battery power thresholds
#define BATTERY_VOLTAGE_HIGH 4.7
#define BATTERY_VOLTAGE_LOW 3.6
#define BATTERY_VOLTAGE_WARNING_SLEEP 3.55
#define BATTERY_PERCENT_WARNING 20

// enable SD card (currently unused)
#define USE_SDCARD false

// debounce time limit for static activities
#define MIN_ACTIVITY_RESTART_SECS 5

// network settings
#define WIFI_TIMEOUT_MS (20 * SECOND) // 20 second WiFi connection timeout
#define WIFI_RECOVER_TIME_MS (30 * SECOND)    // Wait 30 seconds after a failed connection attempt

// input debounce
#define DEBOUNCE_DELAY_MS (SECOND / 2)

// MQTT message sizes
#define MESSAGE_BUFFER_SIZE 2048

// debug settings
#define DEBUG_STACK false
#define DEBUG_PRINT false

// MQTT
#define MQTT_TIMEOUT_MS (20 * SECOND)      // 20 second MQTT connection timeout
#define MQTT_RECOVER_TIME_MS (30 * SECOND) // Wait 30 seconds after a failed connection attempt
#define MQTT_RESEND_CONFIG_EVERY 10
#define MQTT_RETAIN_SENSOR_VALUE true

#if !defined MQTT_NODE_ID
#define MQTT_NODE_ID HOSTNAME
#endif

#if !defined MQTT_DEVICE_NAME
#define MQTT_DEVICE_NAME "HomePlate"
#endif

// Sleep
#define SLEEP_TIMEOUT_SEC 15

// Device Models (from Inkplate-Arduino-library/src/include/defines.h)
#ifdef ARDUINO_ESP32_DEV
#define DEVICE_MODEL "Inkplate 6"
#elif ARDUINO_INKPLATE5
#define DEVICE_MODEL "Inkplate 5"
#elif ARDUINO_INKPLATE10
#define DEVICE_MODEL "Inkplate 10"
#elif ARDUINO_INKPLATE6PLUS
#define DEVICE_MODEL "Inkplate 6PLUS"
#elif ARDUINO_INKPLATECOLOR
#define DEVICE_MODEL "Inkplate 6COLOR"
#elif ARDUINO_INKPLATE2
#define DEVICE_MODEL "Inkplate 2"
#else
#define DEVICE_MODEL "Inkplate (other)"
#endif
