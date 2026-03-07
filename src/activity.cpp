#include "homeplate.h"

#define ACTIVITY_TASK_PRIORITY 4

static bool resetActivity = false;
static SemaphoreHandle_t resetActivityMutex = xSemaphoreCreateMutex();
static SemaphoreHandle_t startActivityMutex = xSemaphoreCreateMutex();
uint activityCount = 0;
uint timeToSleep = 0;

QueueHandle_t activityQueue = xQueueCreate(1, sizeof(Activity));

static unsigned long lastActivityTime = 0;
static Activity activityNext, activityCurrent = NONE;

Activity activityFromString(const char *s)
{
    if (!s) return HomeAssistant;
    if (strcmp(s, "HomeAssistant") == 0) return HomeAssistant;
    if (strcmp(s, "Trmnl") == 0) return Trmnl;
    if (strcmp(s, "GuestWifi") == 0) return GuestWifi;
    if (strcmp(s, "Info") == 0) return Info;
    if (strcmp(s, "Message") == 0) return Message;
    if (strcmp(s, "IMG") == 0) return IMG;
    return HomeAssistant;
}

const char *activityToString(Activity a)
{
    switch (a) {
        case HomeAssistant: return "HomeAssistant";
        case Trmnl: return "Trmnl";
        case GuestWifi: return "GuestWifi";
        case Info: return "Info";
        case Message: return "Message";
        case IMG: return "IMG";
        case NONE: return "NONE";
        default: return "HomeAssistant";
    }
}

// Helper functions for thread-safe resetActivity access
static void setResetActivity(bool value) {
    if (xSemaphoreTake(resetActivityMutex, 100 / portTICK_PERIOD_MS) == pdTRUE) {
        resetActivity = value;
        xSemaphoreGive(resetActivityMutex);
    }
}

static bool getResetActivity() {
    bool value = false;
    if (xSemaphoreTake(resetActivityMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
        value = resetActivity;
        xSemaphoreGive(resetActivityMutex);
    }
    return value;
}

void startActivity(Activity activity)
{
    Activity defaultAct = activityFromString(plateCfg.defaultActivityStr);
    if (xSemaphoreTake(startActivityMutex, (SECOND) / portTICK_PERIOD_MS) == pdTRUE)
    {
        // dont re-queue main Activity is run within 60 sec and already running
        if (activity == defaultAct && activityCurrent == defaultAct && ((millis() - lastActivityTime) / SECOND < 60))
        {
            Serial.printf("[ACTIVITY] startActivity(%d) main activity already running within time limit, skipping\n", activity);
            xSemaphoreGive(startActivityMutex);
            return;
        }
        // insert into queue
        Serial.printf("[ACTIVITY] startActivity(%d) put into queue\n", activity);
        setResetActivity(true);
        xQueueOverwrite(activityQueue, &activity);
        xSemaphoreGive(startActivityMutex);
    }
    else
    {
        Serial.printf("[ACTIVITY][ERROR] startActivity(%d) unable to take mutex\n", activity);
        return;
    }
}

// returns true if there was an event that should cause the curent activity to stop/break early to start something new
bool stopActivity()
{
    return getResetActivity();
}

void waitForWiFiOrActivityChange()
{
    while (!WiFi.isConnected() && !getResetActivity())
    {
        vTaskDelay(250 / portTICK_PERIOD_MS);
    }
}

void runActivities(void *params)
{
    uint32_t sleepSec = plateCfg.sleepMinutes * 60;
    uint32_t quickSleepSec = plateCfg.quickSleepSec;

    while (true)
    {
        printDebug("[ACTIVITY] loop...");
        printDebugStackSpace();
        waitForOTA(); // block if an OTA is running
        if (xQueueReceive(activityQueue, &activityNext, portMAX_DELAY) != pdTRUE)
        {
            Serial.printf("[ACTIVITY][ERROR] runActivities() unable to read from activityQueue\n");
            vTaskDelay(500 / portTICK_PERIOD_MS);
            continue;
        }
        waitForOTA();
        setResetActivity(false);
        printDebug("[ACTIVITY] runActivities ready...");

        if (activityNext != NONE)
            delaySleep(10);

        // activity debounce
        if ((activityNext == activityCurrent) && ((millis() - lastActivityTime) / SECOND < MIN_ACTIVITY_RESTART_SECS))
        {
            Serial.printf("[ACTIVITY] same activity %d launched withing min window, skipping\n", activityNext);
            continue;
        }
        lastActivityTime = millis();
        activityCurrent = activityNext;
        activityCount++;

        Serial.printf("[ACTIVITY] starting activity: %d\n", activityNext);
        bool doQuickSleep = activityNext == GuestWifi || activityNext == Info;
#ifdef CONFIG_CPP
        TimeInfo time = {
            .dow = getDayOfWeek(true),
            .hour = getHour(),
            .minute = getMinute(),
        };
        SleepDefaults defaults = {
            .normalSleep = sleepSec,
            .quickSleep = quickSleepSec,
        };
        timeToSleep = getSleepDuration(sleepSchedule, sleepScheduleSize, time, defaults, doQuickSleep);
#else
        timeToSleep = doQuickSleep ? quickSleepSec : sleepSec;
#endif

        switch (activityNext)
        {
        case NONE:
            break;
        case HomeAssistant:
            if (strlen(plateCfg.imageUrl) == 0)
            {
                Serial.println("[ACTIVITY] HomeAssistant: no IMAGE_URL configured");
                break;
            }
            delaySleep(15);
            setSleepDuration(timeToSleep);
            waitForWiFiOrActivityChange();
            if (getResetActivity())
            {
                Serial.printf("[ACTIVITY][ERROR] HomeAssistant Activity reset while waiting, aborting...\n");
                continue;
            }
            delaySleep(20);
            drawImageFromURL(plateCfg.imageUrl);
            break;
        case Trmnl:
            if (strlen(plateCfg.trmnlId) == 0)
            {
                Serial.println("[ACTIVITY] Trmnl: no TRMNL_ID configured");
                break;
            }
            delaySleep(15);
            setSleepDuration(timeToSleep);
            waitForWiFiOrActivityChange();
            if (getResetActivity())
            {
                Serial.printf("[ACTIVITY][ERROR] Trmnl Activity reset while waiting, aborting...\n");
                continue;
            }
            delaySleep(20);
            trmnlDisplay(plateCfg.trmnlUrl);
            break;
        case GuestWifi:
            setSleepDuration(timeToSleep);
            displayWiFiQR();
            break;
        case Info:
            setSleepDuration(timeToSleep);
            displayInfoScreen();
            break;
        case Message:
            setSleepDuration(timeToSleep);
            displayMessage();
            break;
        case IMG:
            setSleepDuration(timeToSleep);
            waitForWiFiOrActivityChange();
            if (getResetActivity())
            {
                Serial.printf("[ACTIVITY][ERROR] IMG Activity reset while waiting, aborting...\n");
                continue;
            }
            delaySleep(20);
            drawImageFromURL(getMessage());
            break;
        default:
            Serial.printf("[ACTIVITY][ERROR] runActivities() unhandled Activity: %d\n", activityNext);
        }
        displayBatteryWarning();
        sendMQTTStatus();
    }
}

void startActivitiesTask()
{
    startMQTTStatusTask();

    xTaskCreate(
        runActivities,
        "ACTIVITY_TASK",
        8192,
        NULL,
        ACTIVITY_TASK_PRIORITY,
        NULL
    );
}
