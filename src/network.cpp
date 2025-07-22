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
    char ip_str[16];
    WiFi.localIP().toString().toCharArray(ip_str, sizeof(ip_str));
    Serial.printf("[WIFI] IP address: %s\n", ip_str);
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

uint8_t* httpGetRetry(uint32_t trys, const char* url, std::map<String, String> *headers, int32_t* defaultLen, uint32_t timeout_sec) {
    uint8_t* ret = 0;
    for (uint32_t i = 0; i < trys; i++) {
        Serial.printf("[NET] download attempt: %d\n", i);
        ret = httpGet(url, headers, defaultLen,timeout_sec);
        if (ret != nullptr) {
            return ret;
        }
        // wait before trying again
        vTaskDelay(1 * SECOND/portTICK_PERIOD_MS);
    }
    return ret;
}


uint8_t* httpGet(const char* url, std::map<String, String> *headers, int32_t* defaultLen, uint32_t timeout_sec) {
    // Input validation
    if (!url || strlen(url) == 0) {
        Serial.println("[NET] Invalid URL: null or empty");
        return nullptr;
    }
    
    // Basic URL format validation
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        Serial.printf("[NET] Invalid URL protocol: %s\n", url);
        return nullptr;
    }
    
    Serial.printf("[NET] downloading file at URL %s\n", url);

    bool sleep = WiFi.getSleep();
    WiFi.setSleep(false);

    HTTPClient http;
    http.getStream().setNoDelay(true);
    // Set connection timeout (for establishing the connection)
    //http.setConnectTimeout(2000); // 2 seconds
    //http.getStream().setTimeout(timeout_sec); // TODO this seems to have no effect..
    delaySleep(timeout_sec);

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

    // Validate size to prevent buffer overflow attacks
    const int32_t MAX_HTTP_BUFFER_SIZE = 1024 * 1024; // 1MB limit
    if (size <= 0 || size > MAX_HTTP_BUFFER_SIZE) {
        Serial.printf("[NET] Invalid or excessive buffer size: %d bytes\n", size);
        http.end();
        WiFi.setSleep(sleep);
        return nullptr;
    }

    uint8_t* buffer = (uint8_t *)ps_malloc(size);
    if (buffer == nullptr) {
        Serial.printf("[NET] Failed to allocate %d bytes\n", size);
        http.end();
        WiFi.setSleep(sleep);
        return nullptr;
    }
    uint8_t *buffPtr = buffer;

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

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[NET] Non-200 response: %d from URL %s\n", httpCode, url);
        if (size) {
            Serial.printf("[NET] HTTP response buffer: \n\n%s\n\n", buffer);
        }
        free(buffer);
        buffer = 0;
    }

    http.end();
    WiFi.setSleep(sleep);

    return buffer;
}