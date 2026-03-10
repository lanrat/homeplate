#pragma once

#include <Inkplate.h>
#include <map>
// Font includes are per-device tier (below)
#include "sleep_duration.h"

// Config: include user's config.h if it exists, then defaults
#if __has_include("config.h")
#include "config.h"
#endif
#include "config_defaults.h"
#include "config_manager.h"

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

// Reference resolution (Inkplate 10) for proportional scaling
#define REF_WIDTH 1200
#define REF_HEIGHT 825

// Compile-time proportional scaling macros
#define scaleX(px) ((int32_t)(px) * E_INK_WIDTH / REF_WIDTH)
#define scaleY(px) ((int32_t)(px) * E_INK_HEIGHT / REF_HEIGHT)

// Font roles and tier-specific font includes
#if defined(ARDUINO_INKPLATE10) || defined(ARDUINO_INKPLATE10V2) \
 || defined(ARDUINO_INKPLATE6PLUS) || defined(ARDUINO_INKPLATE6PLUSV2) \
 || defined(ARDUINO_INKPLATE6FLICK) || defined(ARDUINO_INKPLATE5V2)
  // Large tier: 720-825px height
  #include "fonts/Roboto_12.h"
  #include "fonts/Roboto_16.h"
  #include "fonts/Roboto_32.h"
  #include "fonts/Roboto_48.h"
  #include "fonts/Roboto_64.h"
  #define FONT_SPLASH  Roboto_64
  #define FONT_TITLE   Roboto_48
  #define FONT_HEADING Roboto_32
  #define FONT_BODY    Roboto_16
  #define FONT_SMALL   Roboto_12
#elif defined(ARDUINO_INKPLATE5) || defined(ARDUINO_ESP32_DEV) || defined(ARDUINO_INKPLATE6V2)
  // Medium tier: 540-600px height
  #include "fonts/Roboto_8.h"
  #include "fonts/Roboto_12.h"
  #include "fonts/Roboto_16.h"
  #include "fonts/Roboto_24.h"
  #include "fonts/Roboto_48.h"
  #include "fonts/Roboto_64.h"
  #define FONT_SPLASH  Roboto_64
  #define FONT_TITLE   Roboto_48
  #define FONT_HEADING Roboto_24
  #define FONT_BODY    Roboto_12
  #define FONT_SMALL   Roboto_8
#else
  // Small tier: IP6Color (448px) and fallback
  #include "fonts/Roboto_8.h"
  #include "fonts/Roboto_12.h"
  #include "fonts/Roboto_16.h"
  #include "fonts/Roboto_32.h"
  #include "fonts/Roboto_64.h"
  #define FONT_SPLASH  Roboto_64
  #define FONT_TITLE   Roboto_32
  #define FONT_HEADING Roboto_16
  #define FONT_BODY    Roboto_12
  #define FONT_SMALL   Roboto_8
#endif

#if defined(ARDUINO_INKPLATE10) \
    || defined(ARDUINO_INKPLATE10V2) \
    || defined(ARDUINO_INKPLATE6V2) \
    || defined(ARDUINO_INKPLATE6PLUS) \
    || defined(ARDUINO_INKPLATE6PLUSV2) \
    || defined(ARDUINO_INKPLATE6FLICK) \
    || defined(ARDUINO_INKPLATECOLOR) \
    || defined(ARDUINO_INKPLATE5) \
    || defined(ARDUINO_INKPLATE5V2)
#define WAKE_BUTTON GPIO_NUM_36
// the original Inkplate 6 does not have a wake button
#elif defined(ARDUINO_ESP32_DEV)
#define WAKE_BUTTON GPIO_NUM_13
#endif

// Boards with capacitive touchpads (PAD1, PAD2, PAD3)
#if defined(ARDUINO_INKPLATE10) || defined(ARDUINO_ESP32_DEV)
#define HAS_TOUCHPADS
#endif

#ifndef VERSION
#define VERSION "dev"
#endif

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
bool drawImageFromBuffer(uint8_t *buff, size_t size, bool center = true);
bool drawPngFromBuffer(uint8_t *buf, int32_t len, int x, int y, bool dither, bool invert);
uint16_t centerTextX(const char *t, int16_t x1, int16_t x2, int16_t y, bool lock = true);
void displayStatusMessage(const char *format, ...);
void splashScreen();

// Trmnl
bool trmnlDisplay(const char *url);
void trmnlLogAdd(const char *message);
void trmnlLogSend();

// Input
void startMonitoringButtonsTask();
void checkBootPads();
void setupWakePins();

// Sleep
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
void mqttSendLowBatteryAlert(double voltage);

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
int httpPost(const char* url, std::map<String, String> *headers, const char* body);

// message
// Font array for findFontSizeFit() — defined in main.cpp (largest to smallest)
extern const GFXfont *fonts[];
extern const size_t fontsCount;
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

Activity activityFromString(const char *s);
const char *activityToString(Activity a);

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

// MQTT discovery topic (compile-time constant)
#define MQTT_DISCOVERY_TOPIC "homeassistant"

// Sleep
#define SLEEP_TIMEOUT_SEC 15
#define MAX_REFRESH_SEC 60*60*24 // 1 day


// Device Models (board defines from Inkplate-Arduino-library/src/include/defines.h)
#if defined(ARDUINO_INKPLATE2)
#define DEVICE_MODEL "Inkplate 2"
#elif defined(ARDUINO_INKPLATE4)
#define DEVICE_MODEL "Inkplate 4"
#elif defined(ARDUINO_INKPLATE4TEMPERA)
#define DEVICE_MODEL "Inkplate 4 Tempera"
#elif defined(ARDUINO_INKPLATE5)
#define DEVICE_MODEL "Inkplate 5"
#elif defined(ARDUINO_INKPLATE5V2)
#define DEVICE_MODEL "Inkplate 5v2"
#elif defined(ARDUINO_ESP32_DEV)
#define DEVICE_MODEL "Inkplate 6"
#elif defined(ARDUINO_INKPLATE6V2)
#define DEVICE_MODEL "Inkplate 6v2"
#elif defined(ARDUINO_INKPLATE6PLUS)
#define DEVICE_MODEL "Inkplate 6 Plus"
#elif defined(ARDUINO_INKPLATE6PLUSV2)
#define DEVICE_MODEL "Inkplate 6 Plusv2"
#elif defined(ARDUINO_INKPLATECOLOR)
#define DEVICE_MODEL "Inkplate 6 Color"
#elif defined(ARDUINO_INKPLATE6FLICK)
#define DEVICE_MODEL "Inkplate 6 Flick"
#elif defined(ARDUINO_INKPLATE7)
#define DEVICE_MODEL "Inkplate 7"
#elif defined(ARDUINO_INKPLATE10)
#define DEVICE_MODEL "Inkplate 10"
#elif defined(ARDUINO_INKPLATE10V2)
#define DEVICE_MODEL "Inkplate 10v2"
#else
#define DEVICE_MODEL "Inkplate (other)"
#endif
