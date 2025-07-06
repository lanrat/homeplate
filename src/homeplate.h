#pragma once

#include <Inkplate.h>
#include <map>
#include "fonts/Roboto_12.h"
#include "fonts/Roboto_16.h"
#include "fonts/Roboto_32.h"
#include "fonts/Roboto_64.h"
#include "fonts/Roboto_128.h"
#include "sleep_duration.h"
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
extern uint bootCount, activityCount, timeToSleep;

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
bool drawImageFromURL(const char *url);
bool drawImageFromBuffer(uint8_t *buff, size_t size);
bool drawPngFromBuffer(uint8_t *buf, int32_t len, int x, int y, bool dither, bool invert);
uint16_t centerTextX(const char *t, int16_t x1, int16_t x2, int16_t y, bool lock = true);
void displayStatusMessage(const char *format, ...);
void splashScreen();

// Trmnl
bool trmnlDisplay(const char *url);

// Input
void startMonitoringButtonsTask();
void checkBootPads();
void setupWakePins();

// Sleep
#define TIME_TO_SLEEP_SEC (TIME_TO_SLEEP_MIN * 60)    // How long ESP32 will be in deep sleep (in seconds)
#ifndef TIME_TO_QUICK_SLEEP_SEC
#define TIME_TO_QUICK_SLEEP_SEC 5 * 60 // 5 minutes. How long ESP32 will be in deep sleep (in seconds) for short activities
#endif
#ifndef MQTT_EXPIRE_AFTER_SEC
#define MQTT_EXPIRE_AFTER_SEC (TIME_TO_SLEEP_SEC * 2)
#endif

void startSleep();
void setSleepDuration(uint32_t sec);
uint32_t getSleepDuration();
void gotoSleepNow();

// time
void setupTimeAndSyncTask();
bool getNTPSynced();
String timeString();
String fullDateString();
int getDayOfWeek(bool weekStartsOnMonday = false);
int getHour();
int getMinute();

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

// network
uint8_t* httpGet(const char* url, std::map<String, String> *headers, int32_t* defaultLen, uint32_t timeout_sec = 5);
uint8_t* httpGetRetry(uint32_t trys, const char* url, std::map<String, String> *headers, int32_t* defaultLen, uint32_t timeout_sec);

// message
static const GFXfont *fonts[] = {&Roboto_128, &Roboto_64, &Roboto_32, &Roboto_16, &Roboto_12};
struct FontSizing
{
    const GFXfont *font;
    uint16_t height;
    uint16_t width;
    uint8_t lineHeight;
    uint8_t yAdvance;
};
FontSizing findFontSizeFit(const char *m, uint16_t max_width, uint16_t max_height);
void setMessage(const char *m);
void displayMessage(const char * = NULL);
const char* getMessage();

// activity
enum Activity
{
    NONE,
    HomeAssistant,
    Trmnl,
    GuestWifi,
    Info,
    Message,
    IMG,
};

#ifndef DEFAULT_ACTIVITY
#define DEFAULT_ACTIVITY HomeAssistant
#endif
void startActivity(Activity activity);
void startActivitiesTask();
bool stopActivity();
void sleepTask();
void delaySleep(uint seconds);

/*
 * Global Settings
 */

// Battery power thresholds
#define BATTERY_VOLTAGE_HIGH 4.2		// for 3.7V nominal battery
#define BATTERY_VOLTAGE_LOW 3.4			// cut-off is ~3.0V
#define BATTERY_VOLTAGE_WARNING_SLEEP 3.2	// prevents deep discharge => longer battery live
#define BATTERY_PERCENT_WARNING 10		// =3.48V

// enable SD card (currently unused)
#define USE_SDCARD false

// set some display defaults
#define USE_DITHERING false
#define DISPLAY_MODE INKPLATE_3BIT

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
#define MAX_REFRESH_SEC 60*60*24 // 1 day


// Device Models (from Inkplate-Arduino-library/src/include/defines.h)
#ifdef ARDUINO_ESP32_DEV
#define DEVICE_MODEL "Inkplate 6"
#elif ARDUINO_INKPLATE6V2
#define DEVICE_MODEL "Inkplate 6v2"
#elif ARDUINO_INKPLATE5
#define DEVICE_MODEL "Inkplate 5"
#elif ARDUINO_INKPLATE10
#define DEVICE_MODEL "Inkplate 10"
#elif ARDUINO_INKPLATE10V2
#define DEVICE_MODEL "Inkplate 10v2"
#elif ARDUINO_INKPLATE6PLUS
#define DEVICE_MODEL "Inkplate 6 PLUS"
#elif ARDUINO_INKPLATE6PLUSV2
#define DEVICE_MODEL "Inkplate 6 PLUSv2"
#elif ARDUINO_INKPLATECOLOR
#define DEVICE_MODEL "Inkplate 6COLOR"
#elif ARDUINO_INKPLATE4
#define DEVICE_MODEL "Inkplate 4"
#elif ARDUINO_INKPLATE2
#define DEVICE_MODEL "Inkplate 2"
#else
#define DEVICE_MODEL "Inkplate (other)"
#endif
