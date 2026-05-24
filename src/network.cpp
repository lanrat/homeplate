#include <WiFi.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include "homeplate.h"
#include "bufferstream.h"

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

// Owns the mDNS responder for the whole device. Started once on first
// WiFi connect and never stopped — components that want to advertise a
// service just call MDNS.addService(...) / MDNS.removeService(...) on
// top of this. Centralized so OpenDisplay and ArduinoOTA don't fight
// over MDNS.begin() (which resets the responder and wipes all services).
static bool s_mdnsStarted = false;

void mdnsStart()
{
    if (s_mdnsStarted) return;
    if (MDNS.begin(plateCfg.hostname)) {
        s_mdnsStarted = true;
        Serial.printf("[MDNS] responder up as %s.local\n", plateCfg.hostname);
    } else {
        Serial.println("[MDNS] responder failed to start");
    }
}

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info)
{
    wifiFailed = false;
    char ip_str[16];
    WiFi.localIP().toString().toCharArray(ip_str, sizeof(ip_str));
    Serial.printf("[WIFI] IP address: %s\n", ip_str);
    mdnsStart();
    displayStatusMessage("WiFi connected");
}

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info)
{
    Serial.println("[WIFI] Disconnected from WiFi access point");
    Serial.print("[WIFI] WiFi lost connection. Reason: ");
    Serial.println(info.wifi_sta_disconnected.reason);
}

// configureWiFi: apply STA mode, hostname, and (optional) static IP from
// plateCfg. Idempotent. Call BEFORE WiFi.begin() / wm.autoConnect() so the
// initial DHCP request announces our hostname instead of the default
// `esp32-<MAC>`. Order matters: WIFI_STA must be set before setHostname()
// so the STA netif exists when the hostname is applied.
void configureWiFi()
{
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
        configureWiFi();
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

uint8_t* httpGetRetry(uint32_t trys, const char* url, std::map<String, String> *headers, int32_t* defaultLen, uint32_t timeout_sec, std::map<String, String> *responseHeadersOut) {
    uint8_t* ret = 0;
    for (uint32_t i = 0; i < trys; i++) {
        Serial.printf("[NET] download attempt: %d\n", i);
        ret = httpGet(url, headers, defaultLen, timeout_sec, responseHeadersOut);
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

    WakeLock lock("http-post", 30);
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

uint8_t* httpGet(const char* url, std::map<String, String> *headers, int32_t* defaultLen, uint32_t timeout_sec, std::map<String, String> *responseHeadersOut) {
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

    WakeLock lock("http-get", timeout_sec + 5);
    HTTPClient http;

    // Connect with HTTP
    http.begin(url);

    if (headers) {
        for (const auto& header : *headers) {
            http.addHeader(header.first, header.second);
        }
    }

    static const char* watchedHeaders[] = { "X-Dither" };
    if (responseHeadersOut) {
        http.collectHeaders(watchedHeaders, sizeof(watchedHeaders) / sizeof(watchedHeaders[0]));
    }

    int httpCode = http.GET();

    if (responseHeadersOut) {
        // collectHeaders() reserves a slot per watched header even when the
        // server didn't send it; http.header(i) then returns "". Skip empties
        // so callers can use find() as "header was actually present".
        for (int i = 0; i < http.headers(); i++) {
            String val = http.header(i);
            if (val.length() == 0) continue;
            (*responseHeadersOut)[http.headerName(i)] = val;
        }
    }

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
    BufferStream bs(buffer, size);
    int written = http.writeToStream(&bs);

    if (written < 0) {
        Serial.printf("[NET] writeToStream error: %d\n", written);
        free(buffer);
        http.end();
        WiFi.setSleep(sleep);
        return nullptr;
    }
    *defaultLen = (int32_t)bs.bytesWritten();

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[NET] Non-200 response: %d from URL %s\n", httpCode, url);
        if (*defaultLen > 0) {
            Serial.printf("[NET] HTTP response buffer: \n\n%s\n\n", buffer);
        }
        free(buffer);
        buffer = 0;
    }

    http.end();
    WiFi.setSleep(sleep);

    return buffer;
}
