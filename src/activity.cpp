#include "homeplate.h"

#define ACTIVITY_TASK_PRIORITY 4

static bool resetActivity = false;
uint activityCount = 0;
uint timeToSleep = TIME_TO_SLEEP_SEC;

QueueHandle_t activityQueue = xQueueCreate(1, sizeof(Activity));

static unsigned long lastActivityTime = 0;
static Activity activityNext, activityCurrent = NONE;

void startActivity(Activity activity)
{
    static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    if (xSemaphoreTake(mutex, (SECOND) / portTICK_PERIOD_MS) == pdTRUE)
    {
        // dont re-queue main Activity is run within 60 sec and already running
        if (activity == DEFAULT_ACTIVITY && activityCurrent == DEFAULT_ACTIVITY && ((millis() - lastActivityTime) / SECOND < 60))
        {
            Serial.printf("[ACTIVITY] startActivity(%d) main activity already running within time limit, skipping\n", activity);
            xSemaphoreGive(mutex);
            return;
        }
        // insert into queue
        Serial.printf("[ACTIVITY] startActivity(%d) put into queue\n", activity);
        resetActivity = true;
        xQueueOverwrite(activityQueue, &activity);
        xSemaphoreGive(mutex);
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
    return resetActivity;
}

void waitForWiFiOrActivityChange()
{
    while (!WiFi.isConnected() && !resetActivity)
    {
        vTaskDelay(250 / portTICK_PERIOD_MS);
    }
}

void runActivities(void *params)
{
    while (true)
    {
        printDebug("[ACTIVITY] loop...");
        printDebugStackSpace();
        waitForOTA(); // block if an OTA is running
        if (xQueueReceive(activityQueue, &activityNext, portMAX_DELAY) != pdTRUE)
        {
            Serial.printf("[ACTIVITY][ERROR] runActivities() unable to read from activityQueue\n");
            vTaskDelay(500 / portTICK_PERIOD_MS); // wait just to be safe
            continue;
        }
        waitForOTA();
        resetActivity = false;
        printDebug("[ACTIVITY] runActivities ready...");

        if (activityNext != NONE)
            delaySleep(10); // bump the sleep timer a little for any ongoing activity

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
            .normalSleep = TIME_TO_SLEEP_SEC,
            .quickSleep = TIME_TO_QUICK_SLEEP_SEC,
        };
        timeToSleep = getSleepDuration(sleepSchedule, sleepScheduleSize, time, defaults, doQuickSleep);
#else
        timeToSleep = doQuickSleep ? TIME_TO_QUICK_SLEEP_SEC : TIME_TO_SLEEP_SEC;
#endif

        switch (activityNext)
        {
        case NONE:
            break;
#ifdef IMAGE_URL
        case HomeAssistant:
            delaySleep(15);
            setSleepDuration(timeToSleep);
            // wait for wifi or reset activity
            waitForWiFiOrActivityChange();
            if (resetActivity)
            {
                Serial.printf("[ACTIVITY][ERROR] HomeAssistant Activity reset while waiting, aborting...\n");
                continue;
            }
            // get & render hass image
            delaySleep(20);
            remotePNG(IMAGE_URL);
            // delaySleep(10);
            break;
#endif
#ifdef TRMNL_ID
        case Trmnl:
            delaySleep(15);
            setSleepDuration(timeToSleep);
            // wait for wifi or reset activity
            waitForWiFiOrActivityChange();
            if (resetActivity)
            {
                Serial.printf("[ACTIVITY][ERROR] Trmnl Activity reset while waiting, aborting...\n");
                continue;
            }
            delaySleep(20);
            trmnlDisplay(TRMNL_URL);
            // delaySleep(10);
            break;
# endif
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
            if (resetActivity)
            {
                Serial.printf("[ACTIVITY][ERROR] IMG Activity reset while waiting, aborting...\n");
                continue;
            }
            // get & render image
            delaySleep(20);
            remotePNG(getMessage());
            break;
        default:
            Serial.printf("[ACTIVITY][ERROR] runActivities() unhandled Activity: %d\n", activityNext);
        }
        // check and display a low battery warning if needed
        displayBatteryWarning();

        // send new MQTT status
        sendMQTTStatus();
    }
}

void startActivitiesTask()
{
    // start the main loop stuff
    startMQTTStatusTask();

    xTaskCreate(
        runActivities,
        "ACTIVITY_TASK",        // Task name
        8192,                   // Stack size
        NULL,                   // Parameter
        ACTIVITY_TASK_PRIORITY, // Task priority
        NULL                    // Task handle
    );
}