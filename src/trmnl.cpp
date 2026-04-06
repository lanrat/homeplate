#include <ArduinoJson.h>
#include "homeplate.h"


RTC_DATA_ATTR char current_filename[64] = "";

// TRMNL Log support
#define TRMNL_LOG_MAX_ENTRIES 8

static JsonDocument trmnlLogDoc;
static int trmnlLogCount = 0;

static void trmnlLogAddDeviceStatus(JsonObject entry) {
    entry["created_at"] = (long)time(nullptr);
    entry["wifi_signal"] = WiFi.RSSI();
    entry["wifi_status"] = WiFi.status();
    entry["refresh_rate"] = getSleepDuration();
    entry["sleep_duration"] = (long)(millis() / 1000);
    entry["wake_reason"] = bootReason();
    entry["free_heap_size"] = ESP.getFreeHeap();
    entry["max_alloc_size"] = ESP.getMaxAllocHeap();

    char ver[50];
    snprintf(ver, sizeof(ver), "Homeplate %s", VERSION);
    entry["firmware_version"] = ver;

    i2cStart();
    double voltage = display.readBattery();
    i2cEnd();
    if (voltage > 0) {
        entry["battery_voltage"] = voltage;
    }
}

void trmnlLogAdd(const char *message) {
    if (!plateCfg.trmnlEnableLog || strlen(plateCfg.trmnlToken) == 0) return;
    if (trmnlLogCount >= TRMNL_LOG_MAX_ENTRIES) {
        Serial.printf("[TRMNL_LOG] Log buffer full, dropping: %s\n", message);
        return;
    }
    JsonObject entry = trmnlLogDoc["logs"].add<JsonObject>();
    entry["message"] = message;
    trmnlLogAddDeviceStatus(entry);
    trmnlLogCount++;
    Serial.printf("[TRMNL_LOG] Added: %s\n", message);
}

void trmnlLogSend() {
    if (!plateCfg.trmnlEnableLog || strlen(plateCfg.trmnlToken) == 0) return;
    if (trmnlLogCount == 0) return;

    // derive log URL from trmnl URL: replace /display with /log
    String logUrl = String(plateCfg.trmnlUrl);
    logUrl.replace("/display", "/log");

    std::map<String, String> headers = {
        {"Access-Token", plateCfg.trmnlToken},
        {"Content-Type", "application/json"},
    };

    char buff[1024];
    serializeJson(trmnlLogDoc, buff, sizeof(buff));
    Serial.printf("[TRMNL_LOG] Sending %d log(s) to %s: %s\n", trmnlLogCount, logUrl.c_str(), buff);

    int code = httpPost(logUrl.c_str(), &headers, buff);
    if (code >= 200 && code < 300) {
        Serial.println("[TRMNL_LOG] Logs sent successfully");
    } else {
        Serial.printf("[TRMNL_LOG] Failed to send logs, HTTP %d\n", code);
    }

    // clear buffer
    trmnlLogDoc.clear();
    trmnlLogCount = 0;
}

// https://docs.trmnl.com/go/private-api/screens
// https://trmnl.com/api-docs/index.html
bool trmnlDisplay(const char *url)
{
    if (strlen(plateCfg.trmnlId) == 0) {
        Serial.println("[TRMNL] TRMNL_ID not configured");
        return false;
    }
    if (url == NULL) {
         Serial.println("[TRMNL] ERROR: got null url!");
         return false;
    }
    displayStatusMessage("Loading next image...");
    Serial.printf("[TRMNL] API: %s\n", url);
    static int32_t defaultLen = 5000;

    // log boot info
    char boot_msg[128];
    snprintf(boot_msg, sizeof(boot_msg), "boot: count=%u reason=%s sleep=%us", bootCount, bootReason(), getSleepDuration());
    trmnlLogAdd(boot_msg);

    // set headers
    std::map<String, String> headers = {
        {"ID", plateCfg.trmnlId},
        {"Access-Token", plateCfg.trmnlToken},
        {"RSSI", String(WiFi.RSSI())},
        {"Refresh-Rate", String(getSleepDuration())},
        {"Accept", "application/json"},
        {"Width", String(E_INK_WIDTH)},
        {"Height", String(E_INK_HEIGHT)},
        {"Model", DEVICE_MODEL},
    };

    // set voltage and temperature
    i2cStart();
    double voltage = display.readBattery();
    int temp = display.readTemperature();
    i2cEnd();
    if (voltage > 0) {
        char volt_buffer[10];
        dtostrf(voltage, 0, 2, volt_buffer);
        headers["Battery-Voltage"] = volt_buffer;
        uint percent = getBatteryPercent(voltage);
        headers["Percent-Charged"] = String(percent);
        Serial.printf("[TRMNL] Sending battery voltage: %s, percent: %u%%\n", headers["Battery-Voltage"].c_str(), percent);
    }

    // set temperature sensor
    if (temp > 0) {
        char sensor_buffer[128];
        snprintf(sensor_buffer, sizeof(sensor_buffer),
            "make=TI;model=TPS65186;kind=temperature;value=%d;unit=celsius;created_at=%ld",
            temp, (long)time(nullptr));
        headers["SENSORS"] = sensor_buffer;
        Serial.printf("[TRMNL] Sending sensor data: %s\n", sensor_buffer);
    }

    // set version
    char ver_buffer[50];
    snprintf(ver_buffer, 50, "%s", VERSION);
    headers["FW-Version"] = ver_buffer;

    uint8_t *buff = httpGet(url, &headers, &defaultLen);
    if (!buff)
    {
        Serial.println("[TRMNL] Download failed");
        displayStatusMessage("Download failed!");
        trmnlLogAdd("error: download failed");
        trmnlLogSend();
        return false;
    }

    // filter here
    JsonDocument trmnl_display_filter;
    trmnl_display_filter["filename"] = true;
    trmnl_display_filter["image_url"] = true;
    trmnl_display_filter["refresh_rate"] = true;
    trmnl_display_filter["status"] = true;
    trmnl_display_filter["error"] = true;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, buff, defaultLen, DeserializationOption::Filter(trmnl_display_filter));
    if (error)
    {
      Serial.printf("[TRMNL][ERROR] JSON Deserialize error: %s\n", error.c_str());
      Serial.printf("[TRMNL][ERROR] JSON: %.*s\n", defaultLen, buff);
      free(buff);
      char err_msg[128];
      snprintf(err_msg, sizeof(err_msg), "error: JSON parse failed: %s", error.c_str());
      trmnlLogAdd(err_msg);
      trmnlLogSend();
      return false;
    }

    free(buff);
    Serial.printf("[TRMNL] Deserialized json:\n");
    serializeJsonPretty(doc, Serial);
    Serial.printf("\n");

    if (doc["refresh_rate"].is<JsonVariant>())
    {
      uint32_t refresh = doc["refresh_rate"].as<uint32_t>();
      Serial.printf("[TRMNL][REFRESH]: %d\n", refresh);
      setSleepDuration(refresh);
    }

    if (doc["status"].is<JsonVariant>()) {
        uint32_t status = doc["status"].as<int32_t>();
        if (status != 0) {
            Serial.printf("[TRMNL][ERROR]: received non-zero status: %d!\n", status);
        }
    }

    if (doc["error"].is<JsonVariant>()) {
        String error = doc["error"].as<String>();
        Serial.printf("[TRMNL][ERROR]: received error: %s\n", error.c_str());
        displayStatusMessage("Error: %s", error.c_str());
    }

    if (doc["filename"].is<JsonVariant>())
    {
      String filename = doc["filename"].as<String>();
      Serial.printf("[TRMNL] Last filename: %s --> new filename: %s\n", current_filename, filename.c_str());
      if (filename.length() > 0 && filename.equals(current_filename)) {
         Serial.printf("[TRMNL] filename unchanged, not refreshing\n");
         trmnlLogAdd("display: filename unchanged, skipped");
         trmnlLogSend();
         return true;
      }
      // update the saved current_filename
      strncpy(current_filename, filename.c_str(), sizeof(current_filename)-1);
      current_filename[sizeof(current_filename)-1] = '\0'; // Ensure null termination
    }

    if (doc["image_url"].is<JsonVariant>())
    {
        // grab the image and display it
        String image_url = doc["image_url"].as<String>();
        bool ret = drawImageFromURL(image_url.c_str());
        if (!ret) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "Unable to Display Image\n%s", current_filename);
            displayMessage(error_msg);
            trmnlLogAdd("error: failed to render image");
        } else {
            char ok_msg[128];
            snprintf(ok_msg, sizeof(ok_msg), "display: updated %s", current_filename);
            trmnlLogAdd(ok_msg);
        }
        trmnlLogSend();
        return ret;
    } else {
        Serial.printf("[TRMNL][ERROR]: No image_url found!\n");
        displayStatusMessage("Download failed!");
        trmnlLogAdd("error: no image_url in response");
    }
    trmnlLogSend();
    return false;
}
