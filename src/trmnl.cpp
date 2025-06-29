#include <ArduinoJson.h>
#include "homeplate.h"

// https://docs.usetrmnl.com/go/private-api/fetch-screen-content
bool trmnlDisplay(const char *url)
{
    if (url == NULL) {
         Serial.println("[TRMNL] ERROR: got null url!");
         return false;
    }
    displayStatusMessage("Loading next image...");
    Serial.printf("[TRMNL] API: %s\n", url);
    // set len for png image, or set 54373?
    static int32_t defaultLen = E_INK_WIDTH * E_INK_HEIGHT * 4 + 100;

    // set headers
    std::map<String, String> headers = {
        {"ID", TRMNL_ID},
        {"Access-Token", TRMNL_TOKEN},
    };

    // set voltage
    i2cStart();
    double voltage = display.readBattery();
    i2cEnd();
    char buffer[10];
    dtostrf(voltage, 0, 2, buffer);

    if (voltage > 0) {
        headers["Battery-Voltage"] = buffer;
        Serial.printf("[TRMNL] Sending battery voltage: %s\n", headers["Battery-Voltage"].c_str());
    }

    uint8_t *buff = httpGet(url, headers, &defaultLen);
    if (!buff)
    {
        Serial.println("[TRMNL] Download failed");
        displayStatusMessage("Download failed!");
        return false;
    }

    // filter here
    JsonDocument trmnl_display_filter;
    trmnl_display_filter["filename"] = true;
    trmnl_display_filter["image_url"] = true;
    trmnl_display_filter["refresh_rate"] = true;
    trmnl_display_filter["special_function"] = true;
    trmnl_display_filter["status"] = true;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, buff, defaultLen, DeserializationOption::Filter(trmnl_display_filter));
    //DeserializationError error = deserializeJson(doc, buff, defaultLen);
    if (error)
    {
      Serial.printf("[TRMNL][ERROR] JSON Deserialize error: %s\n", error.c_str());
      Serial.printf("[TRMNL][ERROR] JSON: %.*s\n", defaultLen, buff);
      free(buff);
      return false;
    }
    
    // Serial.printf("[TRMNL][DEBUG] raw resp:\n");
    // Serial.printf("%.*s\n", defaultLen, buff);
    // Serial.printf("\n");

    free(buff);
    Serial.printf("[TRMNL] Deserialized json:\n");
    serializeJsonPretty(doc, Serial);
    Serial.printf("\n");

    if (doc.containsKey("refresh_rate"))
    {
      uint32_t refresh = doc["refresh_rate"].as<uint32_t>();
      Serial.printf("[TRMNL][REFRESH]: %d\n", refresh);
      setSleepDuration(refresh);
    }

    if (doc.containsKey("image_url"))
    {
        // grab the image and display it
        return remotePNG(doc["image_url"]);
    } else {
        Serial.printf("[TRMNL][ERROR]: No image_url found!\n");
        displayStatusMessage("Download failed!");
    }
    return false;
}