#include <ArduinoOTA.h>
#include "homeplate.h"

#define OTA_TASK_PRIORITY 2

static bool otaRunning = false;

// if an OTA is running, pause main event tasks
void waitForOTA()
{
    while (otaRunning)
    {
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void ota_handle(void *parameter)
{
    while (true)
    {
        waitForWiFi();
        ArduinoOTA.begin();
        Serial.println("[OTA] OTA ready");

        while (WiFi.isConnected())
        {
            ArduinoOTA.handle();
            vTaskDelay(500 / portTICK_PERIOD_MS);
            //printDebugStackSpace();
        }

        Serial.println("[OTA] OTA stop");
        ArduinoOTA.end();
        //printDebugStackSpace();
    }
}

void startOTATask()
{
    ArduinoOTA.setHostname(HOSTNAME);

    ArduinoOTA
        .onStart([]()
                 {
                     otaRunning = true;
                     String type;
                     if (ArduinoOTA.getCommand() == U_FLASH)
                         type = "sketch";
                     else // U_SPIFFS
                         type = "filesystem";
                    
                    startActivity(NONE);
                    delaySleep(60);

                     // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
                     Serial.printf("[OTA] Start OTA updating %s\n", type.c_str());
                     displayStatusMessage("Start OTA updating %s", type.c_str());
                 })
        .onEnd([]()
               {
                   Serial.println("\n[OTA] End");
                   displayStatusMessage("OTA Finished");
                   //otaRunning = false; on finish should restart OTA, going to sleep can interfere.
               })
        .onProgress([](unsigned int progress, unsigned int total)
                {
                    printDebugStackSpace();
                    Serial.printf("[OTA] Progress: %u%%\r", (progress / (total / 100)));
                })
        .onError([](ota_error_t error)
                 {
                     Serial.printf("[OTA] Error[%u]: ", error);
                     if (error == OTA_AUTH_ERROR)
                         Serial.println("Auth Failed");
                     else if (error == OTA_BEGIN_ERROR)
                         Serial.println("Begin Failed");
                     else if (error == OTA_CONNECT_ERROR)
                         Serial.println("Connect Failed");
                     else if (error == OTA_RECEIVE_ERROR)
                         Serial.println("Receive Failed");
                     else if (error == OTA_END_ERROR)
                         Serial.println("End Failed");
                     displayStatusMessage("OTA Error[%u]", error);
                     otaRunning = false;
                 });

    xTaskCreate(
        ota_handle,        /* Task function. */
        "OTA_TASK",        /* String with name of task. */
        4096,              /* Stack size */
        NULL,              /* Parameter passed as input of the task */
        OTA_TASK_PRIORITY, /* Priority of the task. */
        NULL);             /* Task handle. */
}