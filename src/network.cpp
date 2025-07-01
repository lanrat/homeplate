#include <WiFi.h>
#include <HTTPClient.h>


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
        WiFi.setHostname(HOSTNAME); // only works with DHCP....
        WiFi.mode(WIFI_STA);
#ifdef STATIC_IP
        WiFi.config(ip, gateway, subnet, dns);
#endif
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


uint8_t* httpGet(const char* url, std::map<String, String> *headers, int32_t* defaultLen, uint32_t timeout_sec) {
    Serial.printf("[NET] downloading file at URL %s\n", url);

    bool sleep = WiFi.getSleep();
    WiFi.setSleep(false);

    HTTPClient http;
    http.getStream().setNoDelay(true);
    http.getStream().setTimeout(timeout_sec);

    // const char* headersToCollect[] = {
    //     "X-Next-Refresh",
    // };
    // const size_t numberOfHeaders = 1;
    // http.collectHeaders(headersToCollect, numberOfHeaders);

    // Connect with HTTP
    http.begin(url);

    if (headers) {
        for (const auto& header : *headers) {
            //Serial.printf("[NET][DEBUG] adding http header: %s: %s\n", header.first.c_str(), header.second.c_str());
            http.addHeader(header.first, header.second);
        }
    }

    int httpCode = http.GET();

    int32_t size = http.getSize();
    if (size == -1)
        size = *defaultLen;
    else
        *defaultLen = size;

    uint8_t* buffer = (uint8_t *)ps_malloc(size);
    uint8_t *buffPtr = buffer;

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[NET] Non-200 response from URL %s: %d", url, httpCode);
        return buffer;
    }

    // if (http.hasHeader("X-Next-Refresh")) {
    //     // Get the next refresh header value from the server.
    //     // We use this to determine when to wake up next.
    //     String headerVal = http.header("X-Next-Refresh");
    //     *nextRefresh = headerVal.toInt();
    //     // const char* headerValPtr = headerVal.c_str();
    //     // strcpy(nextRefresh, headerValPtr);
    //     logf(LOG_DEBUG, "received header X-Next-Refresh: %d", *nextRefresh);
    // } else {
    //     logf(LOG_WARNING, "header X-Next-Refresh not found in response");
    // }

    int32_t total = http.getSize();
    int32_t len = total;

    uint8_t buff[512] = {0};

    WiFiClient* stream = http.getStreamPtr();
    while (http.connected() && (len > 0 || len == -1)) {
        size_t size = stream->available();

        if (size) {
            int c = stream->readBytes(
                buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
            memcpy(buffPtr, buff, c);

            if (len > 0) len -= c;
            buffPtr += c;
        } else if (len == -1) {
            len = 0;
        }
    }

    http.end();
    WiFi.setSleep(sleep);

    return buffer;
}