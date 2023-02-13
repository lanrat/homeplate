#include <WiFi.h>
#include "homeplate.h"

#define WIFI_TASK_PRIORITY 2

// static addresses
IPAddress ip, gateway, subnet, dns;
bool wifiFailed = false;

xTaskHandle wifiTaskHandle;

bool getWifIFailed()
{
    return wifiFailed;
}

void waitForWiFi()
{
    while (!WiFi.isConnected())
    {
        vTaskDelay(250 / portTICK_PERIOD_MS);
    }
}

void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info)
{
    Serial.println("[WIFI] Connected to AP successfully!");
}

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info)
{
    wifiFailed = false;
    Serial.printf("[WIFI] IP address: %s\n", WiFi.localIP().toString().c_str());
    displayStatusMessage("WiFi connected");
}

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info)
{
    Serial.println("[WIFI] Disconnected from WiFi access point");
    Serial.print("[WIFI] WiFi lost connection. Reason: ");
    Serial.println(info.wifi_sta_disconnected.reason);
}

void keepWiFiAlive(void *parameter)
{
    printDebug("[WIFI] loop start...");
    while (true)
    {
        printDebug("[WIFI] loop...");
        if (WiFi.status() == WL_CONNECTED)
        {
            vTaskDelay(10000 / portTICK_PERIOD_MS);
            continue;
        }

        Serial.println("[WIFI] Connecting...");
        WiFi.mode(WIFI_STA);
#ifdef STATIC_IP
        WiFi.config(ip, gateway, subnet, dns);
#endif
        WiFi.setHostname(HOSTNAME); // only works with DHCP....
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        unsigned long startAttemptTime = millis();

        // Keep looping while we're not connected and haven't reached the timeout
        while (WiFi.status() != WL_CONNECTED &&
               millis() - startAttemptTime < WIFI_TIMEOUT_MS)
        {
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        // check for WiFi connection failed (or the timeout expired)
        // sleep for a while and then retry.
        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("[WIFI] FAILED");
            displayStatusMessage("WiFi failed!");
            wifiFailed = true;
            // if sleep is enabled, we'll likely sleep before this continues
            vTaskDelay(WIFI_RECOVER_TIME_MS / portTICK_PERIOD_MS);
            continue;
        }

        Serial.println("[WIFI] Connected: " + WiFi.localIP().toString());
        printDebugStackSpace();
    }
}

void wifiConnectTask()
{
    if (wifiTaskHandle != NULL)
    {
        Serial.printf("[WIFI] WiFi Task Already running\n");
        return;
    }
    // delete old config
    WiFi.disconnect(true);

#ifdef STATIC_IP
    if (!ip.fromString(STATIC_IP) || !gateway.fromString(STATIC_GATEWAY) || !subnet.fromString(STATIC_SUBNET) || !dns.fromString(STATIC_DNS))
    {
        Serial.printf("[WIFI] Failed to parse static IP information %s/%s %s %s\n", STATIC_IP, STATIC_SUBNET, STATIC_GATEWAY, STATIC_DNS);
        return;
    }
#endif

    WiFi.onEvent(WiFiStationConnected, ARDUINO_EVENT_WIFI_STA_CONNECTED);
    WiFi.onEvent(WiFiGotIP, ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(WiFiStationDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    printDebug("[WIFI] starting...");
    xTaskCreatePinnedToCore(
        keepWiFiAlive,
        "WIFI_TASK",        // Task name
        4096,               // Stack size
        NULL,               // Parameter
        WIFI_TASK_PRIORITY, // Task priority
        &wifiTaskHandle,    // Task handle
        CONFIG_ARDUINO_RUNNING_CORE);
}

void wifiStopTask()
{
    Serial.println("[WIFI] Stopping and disconnecting...");
    if (wifiTaskHandle != NULL)
    {
        vTaskDelete(wifiTaskHandle);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        wifiTaskHandle = NULL;
    }
}
