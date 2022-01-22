#pragma once

#include <Inkplate.h>
#include "fonts/Roboto_12.h"
#include "fonts/Roboto_16.h"
#include "fonts/Roboto_32.h"
#include "fonts/Roboto_64.h"
#include "fonts/Roboto_128.h"
#include "config.h"

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

// TODO verify
#define MIN_BATTERY_VOLTAGE 3.2

#define LOW_BATTERY_WARNING_PERCENT 20

// Image "colors" (3bit mode)
#define C_BLACK 0
#define C_GREY_1 1
#define C_GREY_2 2
#define C_GREY_3 3
#define C_GREY_4 4
#define C_GREY_5 5
#define C_GREY_6 6
#define C_WHITE 7

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
bool hassImage();
bool drawPngFromBuffer(uint8_t *buff, int32_t len, int x, int y);
uint16_t centerTextX(const char *t, int16_t x1, int16_t x2, int16_t y, bool lock = true);
void displayStatusMessage(const char *format, ...);
void splashScreen();

// Input
void startMonitoringButtonsTask();
void checkBootPads();

// Sleep
#define TIME_TO_SLEEP_SEC 20 * 60 // 20 minutes. How long ESP32 will be in deep sleep (in seconds)
#define TIME_TO_QUICK_SLEEP_SEC 5 * 60 // 5 minutes. How long ESP32 will be in deep sleep (in seconds)
void startSleep();
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

// message
void setMessage(const char * m);
void displayMessage(const char * = NULL);

// activity
enum Activity
{
    NONE,
    HomeAssistant,
    GuestWifi,
    Info,
    Message,
};

#define DEFAULT_ACTIVITY HomeAssistant
void startActivity(Activity activity);
void startActivitiesTask();
bool stopActivity();
void sleepTask();
void delaySleep(uint seconds);
