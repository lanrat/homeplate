#include "homeplate.h"
#include <esp_chip_info.h>
#include <esp_mac.h>
#ifdef INKPLATE_IS_COLOR
#include <esp_task_wdt.h>
#endif

// displayRefresh: wraps display.display(). On color panels, widens the task
// watchdog timeout to cover the multi-second blocking refresh, then restores
// it. Also bumps the sleep timer so it doesn't expire mid-refresh — the
// i2c mutex would safely block gotoSleepNow() anyway, but pushing the timer
// out avoids spurious "waiting" log noise and post-refresh immediate sleep.
void displayRefresh()
{
#ifdef INKPLATE_IS_COLOR
    delaySleep(45); // ACeP refresh is 15-30s; pad generously
    esp_task_wdt_config_t cfg = {
        .timeout_ms = 60000,
        .idle_core_mask = (1U << portNUM_PROCESSORS) - 1U,
        .trigger_panic = true,
    };
    esp_task_wdt_reconfigure(&cfg);
#else
    delaySleep(2);
#endif
    display.display();
#ifdef INKPLATE_IS_COLOR
    cfg.timeout_ms = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000;
    esp_task_wdt_reconfigure(&cfg);
#endif
}

uint getBatteryPercent(double voltage)
{
    if (voltage < BATTERY_VOLTAGE_LOW)
        return 0;

    uint percentage = ((voltage - BATTERY_VOLTAGE_LOW) * 100.0) / (BATTERY_VOLTAGE_HIGH - BATTERY_VOLTAGE_LOW);
    if (percentage > 100)
        percentage = 100;
    if (percentage < 0)
        percentage = 0;
    return percentage;
}

const char *bootReason()
{
    esp_sleep_wakeup_cause_t wakeup_reason;
    wakeup_reason = esp_sleep_get_wakeup_cause();
    switch (wakeup_reason)
    {
    case ESP_SLEEP_WAKEUP_EXT0:
        return "wake button"; // (EXT0_RTC_IO) is the wake button...
    case ESP_SLEEP_WAKEUP_EXT1:
        return "touchpads"; // (EXT1_RTC_CNTL)
    case ESP_SLEEP_WAKEUP_TIMER:
        return "timer";
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
        return "internal touchpad";
    case ESP_SLEEP_WAKEUP_ULP:
        return "ULP program";
    default:
        return "normal";
    }
}

void printChipInfo()
{
    /* Print chip information */
    Serial.printf("ESP Chip information:\n");
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    Serial.printf("This is %s chip with %u CPU core(s), WiFi%s%s, ",
                  CONFIG_IDF_TARGET,
                  chip_info.cores,
                  (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
                  (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    Serial.printf("silicon revision %u, ", chip_info.revision);

    Serial.printf("%dMB %s flash\n", ESP.getFlashChipSize() / (1024 * 1024),
                  (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    Serial.printf("Minimum free heap size: %u bytes\n", esp_get_minimum_free_heap_size());

    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        Serial.printf("WiFi STA MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    heap_caps_print_heap_info(MALLOC_CAP_32BIT | MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM | MALLOC_CAP_INTERNAL);
}

/**
 * \brief Called if stack overflow during execution
 */
extern void vApplicationStackOverflowHook(xTaskHandle *pxTask,
                                          signed char *pcTaskName)
{
    Serial.printf("\n\n!!! [DEBUG]: stack overflow %x %s\n\n", (unsigned int)pxTask, (portCHAR *)pcTaskName);
    /* If the parameters have been corrupted then inspect pxCurrentTCB to
     * identify which task has overflowed its stack.
     */
    for (;;)
    {
    }
}

void lowBatteryCheck()
{
    // need min uptime for voltage to be stable
    if (millis() < 2 * SECOND)
    {
        return;
    }
    i2cStart();
    double voltage = display.readBattery();
    i2cEnd();

    // if voltage was < 1, it was a bad reading.
    if (voltage > 1 && voltage <= BATTERY_VOLTAGE_WARNING_SLEEP)
    {
        Serial.printf("[MAIN] voltage %.2f <= min %.2f, powering down\n", voltage, BATTERY_VOLTAGE_WARNING_SLEEP);
        displayStatusMessage("Low Battery");
        // send low battery notification via MQTT before sleeping
        if (mqttConnected())
        {
            Serial.println("[MAIN] Sending low battery MQTT notification");
            mqttSendLowBatteryAlert(voltage);
            vTaskDelay(2 * SECOND / portTICK_PERIOD_MS);
        }
        setSleepDuration(0xFFFFFFFF);
        gotoSleepNow();
    }
}

void printDebugStackSpace()
{
    if (DEBUG_STACK)
    {
        UBaseType_t uxHighWaterMark;
        uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
        Serial.printf("[DEBUG][HEAP] TASK: %s; stack min remaining(%d) txPortGetFreeHeapSize(%d) xPortGetMinimumEverFreeHeapSize(%d) bytes\n", pcTaskGetTaskName(NULL), uxHighWaterMark, xPortGetFreeHeapSize(), xPortGetMinimumEverFreeHeapSize());
    }
}

// vTaskGetRunTimeStats and vTaskList are disabled in esp-idf :()
// void taskinfo()
// {
//     Serial.printf("Task info:\n");
//     auto num = uxTaskGetNumberOfTasks();
//     Serial.printf("total tasks: %d\n", num);
//     char buff[40 * num + 40]; // 40 bytes per task + 40
//     vTaskGetRunTimeStats(buff);
//     Serial.printf("vTaskGetRunTimeStats:\n%s", buff);
//     vTaskList(buff);
//     Serial.printf("vTaskList:\n%s", buff);
//     //uxTaskGetSystemState();
// }

// NOTE I2C & display locks MUST NOT be held by caller.
void displayBatteryWarning()
{
    static const uint8_t buf_size = 30;
    static char statusBuffer[buf_size];
    i2cStart();
    double voltage = display.readBattery();
    int percent = getBatteryPercent(voltage);

    if (percent > BATTERY_PERCENT_WARNING)
    {
        i2cEnd();
        return;
    }

    snprintf(statusBuffer, buf_size, "WARNING: battery %u%%", percent);

#ifdef INKPLATE_HAS_PARTIAL_UPDATE
    displayStart();
    display.selectDisplayMode(INKPLATE_1BIT);
    display.setTextColor(HP_FG, HP_BG);
    display.setFont(&FONT_BODY);
    display.setTextSize(1);

    const int16_t pad = 3; // padding
    const int16_t mar = 5; // margin
    int16_t x = E_INK_WIDTH / 2;
    int16_t y = E_INK_HEIGHT - mar;

    // get text size for box
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(statusBuffer, x, y, &x1, &y1, &w, &h);

    x = (E_INK_WIDTH / 2) - (w / 2);

    // background box to set internal buffer colors
    display.fillRect(x - pad, y - pad - h, w + (pad * 2), h + (pad * 2), HP_BG);

    // display status message
    display.setCursor(x, y);

    // text to print over box
    display.print(statusBuffer);
    display.partialUpdate(sleepBoot);
    displayEnd();
#endif
    i2cEnd();
}

void printDebug(const char *s)
{
    if (DEBUG_PRINT)
    {
        Serial.printf("[DEBUG]%s\n", s);
    }
}