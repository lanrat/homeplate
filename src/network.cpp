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

        Serial.println("[WIFI] Reconnecting...");
        WiFi.mode(WIFI_STA);
        WiFi.setHostname(plateCfg.hostname);
        if (strlen(plateCfg.staticIp) > 0)
        {
            if (ip.fromString(plateCfg.staticIp) && gateway.fromString(plateCfg.staticGateway) &&
                subnet.fromString(plateCfg.staticSubnet) && dns.fromString(plateCfg.staticDns))
            {
                WiFi.config(ip, gateway, subnet, dns);
            }
            else
            {
                Serial.printf("[WIFI] Failed to parse static IP information %s/%s %s %s\n",
                              plateCfg.staticIp, plateCfg.staticSubnet, plateCfg.staticGateway, plateCfg.staticDns);
            }
        }
        // Use stored credentials from WiFiManager (no args)
        Serial.printf("[WIFI] Connecting to SSID: %s\n", WiFi.SSID().c_str());
        WiFi.begin();

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

        Serial.printf("[WIFI] Connected: %s BSSID: %s\n", WiFi.localIP().toString().c_str(), WiFi.BSSIDstr().c_str());
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


int httpPost(const char* url, std::map<String, String> *headers, const char* body) {
    if (!url || strlen(url) == 0) {
        Serial.println("[NET] httpPost: Invalid URL");
        return -1;
    }
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        Serial.printf("[NET] httpPost: Invalid URL protocol: %s\n", url);
        return -1;
    }

    Serial.printf("[NET] POST %s\n", url);

    bool sleep = WiFi.getSleep();
    WiFi.setSleep(false);

    HTTPClient http;
    http.begin(url);

    if (headers) {
        for (const auto& header : *headers) {
            http.addHeader(header.first, header.second);
        }
    }

    int httpCode = http.POST(body ? body : "");

    Serial.printf("[NET] POST response: %d\n", httpCode);

    http.end();
    WiFi.setSleep(sleep);

    return httpCode;
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

    // Wait for WiFi to be stable
    waitForWiFi();
    
    bool sleep = WiFi.getSleep();
    WiFi.setSleep(false);

    HTTPClient http;
    // Set longer timeouts (in milliseconds)
    http.setConnectTimeout(10000); // 10 seconds for connection
    http.setTimeout(30000); // 30 seconds for overall timeout
    delaySleep(timeout_sec);

    // Connect with HTTP
    if (!http.begin(url)) {
        Serial.println("[NET] Failed to begin HTTP connection");
        WiFi.setSleep(sleep);
        return nullptr;
    }

    if (headers) {
        for (const auto& header : *headers) {
            http.addHeader(header.first, header.second);
        }
    }

    int httpCode = http.GET();

    if (httpCode < 0) {
        Serial.printf("[NET] HTTP error: %d\n", httpCode);
        // Common error codes:
        // -1: Connection refused
        // -2: Send header failed
        // -3: Send payload failed
        // -4: Not connected
        // -5: Connection lost
        // -6: No stream
        // -7: No HTTP server
        // -8: Too large response
        // -9: 3xx redirect (not followed)
        // -10: 4xx client error (not success)
        // -11: Connection failed / Connection refused
        switch (httpCode) {
            case -1: Serial.println("[NET] Error: Connection refused"); break;
            case -2: Serial.println("[NET] Error: Send header failed"); break;
            case -3: Serial.println("[NET] Error: Send payload failed"); break;
            case -4: Serial.println("[NET] Error: Not connected"); break;
            case -5: Serial.println("[NET] Error: Connection lost"); break;
            case -6: Serial.println("[NET] Error: No stream"); break;
            case -7: Serial.println("[NET] Error: No HTTP server"); break;
            case -11: Serial.println("[NET] Error: Connection failed / refused - check server is running"); break;
            default: Serial.println("[NET] Error: Unknown"); break;
        }
        http.end();
        WiFi.setSleep(sleep);
        return nullptr;
    }

    int32_t size = http.getSize();
    Serial.printf("[NET] Content-Length: %d\n", size);

    // Handle chunked encoding (size == -1)
    if (size == -1) {
        Serial.println("[NET] Chunked encoding detected, will read until end");
    }

    // For chunked encoding or invalid size, use defaultLen
    if (size == -1) {
        size = *defaultLen;
    } else {
        *defaultLen = size;
    }

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
    memset(buffer, 0, size); // Zero out buffer
    uint8_t *buffPtr = buffer;

    int32_t total = http.getSize();
    int32_t len = total;
    int32_t bytesRead = 0;

    uint8_t buff[512] = {0};

    WiFiClient* stream = http.getStreamPtr();
    while (http.connected() && (len > 0 || len == -1)) {
        size_t available = stream->available();

        if (available) {
            int c = stream->readBytes(buff, ((available > sizeof(buff)) ? sizeof(buff) : available));
            if (c > 0 && bytesRead < size) {
                int toCopy = min(c, size - bytesRead);
                memcpy(buffPtr, buff, toCopy);
                buffPtr += toCopy;
                bytesRead += toCopy;
            }

            if (len > 0) len -= c;
        } else if (len == -1) {
            // For chunked encoding, break when no more data available
            vTaskDelay(10 / portTICK_PERIOD_MS);
            if (!stream->available()) {
                break;
            }
        }
    }

    Serial.printf("[NET] Bytes read: %d\n", bytesRead);

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[NET] Non-200 response: %d from URL %s\n", httpCode, url);
        if (bytesRead > 0) {
            Serial.printf("[NET] HTTP response buffer (%d bytes): \n", bytesRead);
            // Print first 256 bytes as hex for debugging
            for (int i = 0; i < min(bytesRead, 256); i++) {
                Serial.printf("%02x ", buffer[i]);
                if ((i + 1) % 16 == 0) Serial.println();
            }
            Serial.println();
        }
        free(buffer);
        WiFi.setSleep(sleep);
        return nullptr;
    }

    http.end();
    WiFi.setSleep(sleep);

    return buffer;
}