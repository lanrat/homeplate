#include "homeplate.h"

#define DEBUG_STACK false

uint getBatteryPercent(double voltage)
{
    static const double voltage_low = 3.35; // 2.5;
    static const double voltage_high = 4.3; // 3.7;
    uint percentage = ((voltage - voltage_low) * 100.0) / (voltage_high - voltage_low);
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

    Serial.printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
                  (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    Serial.printf("Minimum free heap size: %u bytes\n", esp_get_minimum_free_heap_size());

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
	for (;;) {
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
    if (voltage > 1 && voltage <= MIN_BATTERY_VOLTAGE)
    {
        Serial.printf("[MAIN] voltage %.2f <= min %.2f, powering down\n", voltage, MIN_BATTERY_VOLTAGE);
        displayStatusMessage("Low Battery");
        // TODO mqtt send low battery sleep notification
        setSleepDuration(0xFFFFFFFF);
        gotoSleepNow();
    }
}

void printDebugStackSpace()
{
    if (DEBUG_STACK)
    {
        UBaseType_t uxHighWaterMark;
        uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
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

    if (percent > LOW_BATTERY_WARNING_PERCENT)
    {
        i2cEnd();
        return;
    }

    snprintf(statusBuffer, buf_size, "WARNING: battery %u%%", percent);

    displayStart();
    display.selectDisplayMode(INKPLATE_1BIT);
    display.setTextColor(BLACK, WHITE);
    display.setFont(&Roboto_16);
    display.setTextSize(1);

    const int16_t pad = 3;           // padding
    const int16_t mar = 5;           // margin
    int16_t x = E_INK_WIDTH / 2;
    int16_t y = E_INK_HEIGHT - mar;

    // get text size for box
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(statusBuffer, x, y, &x1, &y1, &w, &h);

    x = (E_INK_WIDTH / 2) - (w / 2);

    // background box to set internal buffer colors
    display.fillRect(x - pad, y - pad - h, w + (pad * 2), h + (pad * 2), WHITE);
    //Serial.printf("fillRect(x:%u, y:%u, w:%u, h:%u)\n", x-pad, y-pad-h, max(w+(pad*2), 400), h+(pad*2));
    //display.partialUpdate(sleepBoot);

    // display status message
    display.setCursor(x, y);

    // text to print over box
    display.print(statusBuffer);
    display.partialUpdate(sleepBoot);
    displayEnd();
    i2cEnd();
}