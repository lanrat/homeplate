#include <AsyncMqttClient.h>
#include <ArduinoJson.h>
#include "homeplate.h"

#define MQTT_TASK_PRIORITY 3
#define MQTT_SEND_TASK_PRIORITY 5

#define MAX_MQTT_REFRESH_SEC 60*60*24 // 1 day

#define MQTT_ACTION_TOPIC        "homeplate/" MQTT_NODE_ID "/activity/run"
#define MQTT_DISCOVERY_TOPIC     "homeassistant"
#define mqtt_base_sensor(topic)  MQTT_DISCOVERY_TOPIC "/sensor/" MQTT_NODE_ID "/" topic
#define mqtt_unique_id(id)       MQTT_NODE_ID "_" id

AsyncMqttClient mqttClient;
xTaskHandle mqttTaskHandle;
bool mqttFailed = false;
StaticJsonDocument<200> filter;

static bool mqttWaiting; // is MQTT waiting for status to be sent
static bool mqttRun;     // should another MQTT status update be sent
static bool mqttKill;    // should the status task stop running

const static char *state_topic_wifi_signal = mqtt_base_sensor("wifi_signal/state");
const static char *state_topic_temperature = mqtt_base_sensor("temperature/state");
const static char *state_topic_battery = mqtt_base_sensor("battery/state");
const static char *state_topic_boot = mqtt_base_sensor("boot/state");

bool getMQTTFailed()
{
  return mqttFailed;
}

void waitForMQTT()
{
  while (!mqttClient.connected())
  {
    vTaskDelay(250 / portTICK_PERIOD_MS);
  }
}

bool mqttConnected()
{
  return mqttClient.connected();
}

void mqttSendWiFiStatus()
{
  long rssi = 0;
  static const int samples = 10;
  for (int i = 0; i < samples; i++)
  {
    rssi = rssi + WiFi.RSSI();
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }

  rssi = rssi / samples;

  if (rssi >= 0)
  {
    Serial.printf("[MQTT] Got invalid WiFi RSSI (%ld), not sending status to mqtt\n", rssi);
    return;
  }

  const int capacity = JSON_OBJECT_SIZE(1);
  StaticJsonDocument<capacity> doc;
  doc["signal"] = rssi;
  char buff[256];
  serializeJson(doc, buff);
  Serial.printf("[MQTT] Sending MQTT State: [%s] %s\n", state_topic_wifi_signal, buff);
  mqttClient.publish(state_topic_wifi_signal, 1, MQTT_RETAIN_SENSOR_VALUE, buff);
}

// This can cause screen fade, use sparingly
void mqttSendTempStatus()
{
  int temperature = 0;
  i2cStart();
  displayStart();
  temperature = temperature + display.readTemperature();
  displayEnd();
  i2cEnd();

  if (temperature <= 1)
  {
    Serial.printf("[MQTT] Got invalid temperature (%d), not sending status to mqtt\n", temperature);
    return;
  }

  char buff[256];
  const int capacity = JSON_OBJECT_SIZE(1);
  StaticJsonDocument<capacity> doc;
  doc["temperature"] = temperature;
  serializeJson(doc, buff);
  Serial.printf("[MQTT] Sending MQTT State: [%s] %s\n", state_topic_temperature, buff);
  mqttClient.publish(state_topic_temperature, 1, MQTT_RETAIN_SENSOR_VALUE, buff);
}

void mqttSendBatteryStatus()
{
  double voltage = 0;
  i2cStart();
  voltage = display.readBattery();
  i2cEnd();

  voltage = round(voltage * 100) / 100; // rounds to 2 decimal places

  int percent = getBatteryPercent(voltage);

  if (voltage <= 0.1)
  {
    Serial.printf("[MQTT] Got invalid voltage (%.2f), not sending status to mqtt\n", voltage);
    return;
  }

  char buff[256];
  const int capacity = JSON_OBJECT_SIZE(2);
  StaticJsonDocument<capacity> doc;
  doc["voltage"] = voltage;
  doc["battery"] = percent;
  serializeJson(doc, buff);
  Serial.printf("[MQTT] Sending MQTT State: [%s] %s\n", state_topic_battery, buff);
  mqttClient.publish(state_topic_battery, 1, MQTT_RETAIN_SENSOR_VALUE, buff);
}

void mqttSendBootStatus(uint boot, uint activityCount, const char *bootReason)
{
  char buff[512];
  const int capacity = JSON_OBJECT_SIZE(3);
  StaticJsonDocument<capacity> doc;
  doc["boot"] = boot;
  doc["activity_count"] = activityCount;
  doc["boot_reason"] = bootReason;
  serializeJson(doc, buff);
  Serial.printf("[MQTT] Sending MQTT State: [%s] %s\n", state_topic_boot, buff);
  mqttClient.publish(state_topic_boot, 1, MQTT_RETAIN_SENSOR_VALUE, buff);
}

void sendHAConfig()
{
  // sends MQTT info for auto discovery
  // https://www.home-assistant.io/docs/mqtt/discovery/
  // https://www.home-assistant.io/integrations/sensor.mqtt/

  Serial.println("[MQTT] Sending MQTT Config");
  // retain must be true for config
  const bool retain = true;
  const int qos = 1;
  char buff[768];
  const int capacity = JSON_OBJECT_SIZE(24); // intentionally larger than needed.
  StaticJsonDocument<capacity> doc;

  // macaddr
  // need to copy macaddr because doc.clear() erases macaddr-pointer
  char macaddr[18];
  strncpy(macaddr, WiFi.macAddress().c_str(), 18);

  // deviceinfo
  const int devCapacity = JSON_OBJECT_SIZE(7) + 32; // + sizeof(macaddr) + extra space matching obj size stepping
  StaticJsonDocument<devCapacity> deviceInfo;
  deviceInfo.clear();
  deviceInfo["manufacturer"] = "e-radionica";
  deviceInfo["model"] = DEVICE_MODEL;
  deviceInfo["name"] = MQTT_DEVICE_NAME;
  deviceInfo["sw_version"] = VERSION;
  deviceInfo["identifiers"][0] = MQTT_NODE_ID;
  deviceInfo["identifiers"][1] = macaddr;

  // wifi RSSI
  doc.clear();
  doc["unique_id"] = mqtt_unique_id("wifi_signal");
  doc["device_class"] = "signal_strength";
  doc["state_class"] = "measurement";
  doc["name"] = "WiFi Signal";
  doc["state_topic"] = state_topic_wifi_signal;
  doc["unit_of_measurement"] = "dBm";
  doc["value_template"] = "{{ value_json.signal }}";
  doc["expire_after"] = TIME_TO_SLEEP_SEC * 2;
  doc["entity_category"] = "diagnostic";
  doc["device"] = deviceInfo;
  serializeJson(doc, buff);
  mqttClient.publish(mqtt_base_sensor("wifi_signal/config"), qos, retain, buff);

  // temp
  doc.clear();
  doc["unique_id"] = mqtt_unique_id("temperature");
  doc["device_class"] = "temperature";
  doc["state_class"] = "measurement";
  doc["name"] = "Temperature";
  doc["state_topic"] = state_topic_temperature;
  doc["unit_of_measurement"] = "°C";
  doc["value_template"] = "{{ value_json.temperature }}";
  doc["expire_after"] = TIME_TO_SLEEP_SEC * 2;
  doc["device"] = deviceInfo;
  serializeJson(doc, buff);
  mqttClient.publish(mqtt_base_sensor("temperature/config"), qos, retain, buff);

  // battery
  doc.clear();
  doc["unique_id"] = mqtt_unique_id("voltage");
  doc["device_class"] = "voltage";
  doc["state_class"] = "measurement";
  doc["name"] = "Voltage";
  doc["state_topic"] = state_topic_battery;
  doc["unit_of_measurement"] = "V";
  doc["value_template"] = "{{ value_json.voltage }}";
  doc["expire_after"] = TIME_TO_SLEEP_SEC * 2;
  doc["device"] = deviceInfo;
  serializeJson(doc, buff);
  mqttClient.publish(mqtt_base_sensor("voltage/config"), qos, retain, buff);
  doc.clear();
  doc["unique_id"] = mqtt_unique_id("battery");
  doc["device_class"] = "battery";
  doc["state_class"] = "measurement";
  doc["name"] = "Battery";
  doc["state_topic"] = state_topic_battery;
  doc["unit_of_measurement"] = "%";
  doc["value_template"] = "{{ value_json.battery }}";
  doc["expire_after"] = TIME_TO_SLEEP_SEC * 2;
  doc["device"] = deviceInfo;
  serializeJson(doc, buff);
  mqttClient.publish(mqtt_base_sensor("battery/config"), qos, retain, buff);

  // boot
  doc.clear();
  doc["unique_id"] = mqtt_unique_id("boot");
  doc["state_class"] = "total_increasing";
  doc["name"] = "Boot Count";
  doc["state_topic"] = state_topic_boot;
  doc["unit_of_measurement"] = "boot";
  doc["icon"] = "mdi:chart-line-variant";
  doc["value_template"] = "{{ value_json.boot }}";
  doc["expire_after"] = TIME_TO_SLEEP_SEC * 2;
  doc["entity_category"] = "diagnostic";
  doc["enabled_by_default"] = false;
  doc["device"] = deviceInfo;
  serializeJson(doc, buff);
  mqttClient.publish(mqtt_base_sensor("boot/config"), qos, retain, buff);

  // boot reason
  doc.clear();
  doc["unique_id"] = mqtt_unique_id("boot_reason");
  doc["name"] = "Boot Reason";
  doc["state_topic"] = state_topic_boot;
  doc["value_template"] = "{{ value_json.boot_reason }}";
  doc["expire_after"] = TIME_TO_SLEEP_SEC * 2;
  doc["entity_category"] = "diagnostic";
  doc["enabled_by_default"] = false;
  doc["device"] = deviceInfo;
  serializeJson(doc, buff);
  mqttClient.publish(mqtt_base_sensor("boot_reason/config"), qos, retain, buff);

  // activityCount
  doc.clear();
  doc["unique_id"] = mqtt_unique_id("activity_count");
  doc["state_class"] = "total_increasing";
  doc["name"] = "Activity Count";
  doc["state_topic"] = state_topic_boot;
  doc["unit_of_measurement"] = "activities";
  doc["icon"] = "mdi:chart-line-variant";
  doc["value_template"] = "{{ value_json.activity_count }}";
  doc["expire_after"] = TIME_TO_SLEEP_SEC * 2;
  doc["entity_category"] = "diagnostic";
  doc["enabled_by_default"] = false;
  doc["device"] = deviceInfo;
  serializeJson(doc, buff);
  mqttClient.publish(mqtt_base_sensor("activity_count/config"), qos, retain, buff);
}

void connectToMqtt(void *params)
{
  while (true)
  {
    printDebug("[MQTT] MQTT loop...");
    // if already connected, do nothing
    if (mqttClient.connected())
    {
      vTaskDelay(10000 / portTICK_PERIOD_MS);
      continue;
    }

    waitForWiFi();
    Serial.println("[MQTT] Connecting to MQTT...");
    mqttClient.connect();

    unsigned long startAttemptTime = millis();
    // Keep looping while we're not connected and haven't reached the timeout
    while (!mqttClient.connected() &&
           millis() - startAttemptTime < MQTT_TIMEOUT_MS)
    {
      vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    if (!mqttClient.connected())
    {
      Serial.println("[MQTT] FAILED");
      displayStatusMessage("MQTT failed!");
      mqttFailed = true;
      // if sleep is enabled, we'll likely sleep before this continues
      vTaskDelay(MQTT_RECOVER_TIME_MS / portTICK_PERIOD_MS);
      continue;
    }

    Serial.println("[MQTT] Connected");
    printDebugStackSpace();
  }
}

void onMqttConnect(bool sessionPresent)
{
  mqttFailed = false;
  Serial.println("[MQTT] Connected to MQTT.");
  Serial.print("[MQTT] Session present: ");
  Serial.println(sessionPresent);

  uint16_t packetIdSub = mqttClient.subscribe(MQTT_ACTION_TOPIC, 2);
  Serial.print("[MQTT] Subscribing at QoS 2, packetId: ");
  Serial.println(packetIdSub);

  // only send this on first boot, or after every 10 sleep boots
  // depending on MQTT server configuration , some persistent messages may expire after a while, so we'll resend them
  if (!sleepBoot || bootCount % MQTT_RESEND_CONFIG_EVERY)
    sendHAConfig();
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
  Serial.println("[MQTT] Disconnected from MQTT.");
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos)
{
  Serial.printf("[MQTT] Subscribe acknowledged: packetId: %u qos: %u \n", packetId, qos);
}

void onMqttUnsubscribe(uint16_t packetId)
{
  Serial.println("[MQTT] Unsubscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{
  Serial.println("[MQTT] Publish received.");
  Serial.print("  topic: ");
  Serial.println(topic);
  Serial.print("  qos: ");
  Serial.println(properties.qos);
  // Serial.print("  dup: ");
  // Serial.println(properties.dup);
  Serial.print("  retain: ");
  Serial.println(properties.retain);
  Serial.print("  len: ");
  Serial.println(len);
  // Serial.print("  index: ");
  // Serial.println(index);
  // Serial.print("  total: ");
  // Serial.println(total);
  // Serial.print("  payload: ");
  // Serial.println(payload);

  // if received message is wrong topic, do nothing.
  if (strncmp(topic, MQTT_ACTION_TOPIC, strlen(MQTT_ACTION_TOPIC)) != 0)
  {
    return;
  }

  // only clear if there is a payload, otherwise the MQTT messages are cleared
  if (len > 0)
  {
    // send blank response to clear/ack the MQTT command
    mqttClient.publish(MQTT_ACTION_TOPIC, 1, true);

    StaticJsonDocument<MESSAGE_BUFFER_SIZE> doc;
    DeserializationError error = deserializeJson(doc, payload, len, DeserializationOption::Filter(filter));
    if (error)
    {
      Serial.printf("[MQTT][ERROR] JSON Deserialize error: %s\n", error.c_str());
      Serial.printf("[MQTT][ERROR] JSON: %.*s\n", len, payload);
      return;
    }

    Serial.printf("[MQTT] Deserialized json:\n");
    serializeJsonPretty(doc, Serial);
    Serial.printf("\n");
    const char *action = doc["action"];

    if (doc.containsKey("refresh"))
    {
      int refresh = doc["refresh"].as<int>();
      Serial.printf("[MQTT][REFRESH]: %d\n", refresh);
      if (refresh > 0 && refresh < MAX_MQTT_REFRESH_SEC)
      {
        setSleepRefresh((uint32_t) refresh);
      }
      else
      {
        Serial.printf("[MQTT][ERROR] refresh value is out of range\n");
      }
    }

    if (strncmp("qr", action, 3) == 0)
    {
      startActivity(GuestWifi);
      return;
    }
    else if (strncmp("info", action, 5) == 0)
    {
      startActivity(Info);
      return;
    }
    else if (strncmp("hass", action, 5) == 0)
    {
      startActivity(HomeAssistant);
      return;
    }
    else if (strncmp("message", action, 9) == 0)
    {
      if (!doc.containsKey("message"))
      {
        Serial.printf("[MQTT][ERROR] message action has no message!\n");
        return;
      }
      setMessage(doc["message"]);
      startActivity(Message);
      return;
    }
    else if (strncmp("img", action, 4) == 0)
    {
      if (!doc.containsKey("message"))
      {
        Serial.printf("[MQTT][ERROR] img action has no url message!\n");
        return;
      }
      // use the message buffer to hold the URL
      setMessage(doc["message"]);
      startActivity(IMG);
      return;
    }
    Serial.printf("[MQTT][ERROR] unable to handle action %s\n", action);
  }
}

/*void onMqttPublish(uint16_t packetId)
{
  Serial.println("Publish acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}*/

void startMQTTTask()
{
  if (mqttTaskHandle != NULL)
  {
    Serial.printf("[MQTT] MQTT Task Already running\n");
    return;
  }
  #ifndef MQTT_HOST
    // if no MQTT, exit early
    return;
  #endif
  Serial.printf("[MQTT] starting MQTT\n");
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  // mqttClient.onPublish(onMqttPublish);

  // set deserialization filter
  filter["action"] = true;
  filter["message"] = true;
  filter["refresh"] = true;

  mqttClient.setClientId(HOSTNAME);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
#ifdef MQTT_USER
  mqttClient.setCredentials(MQTT_USER, MQTT_PASSWORD);
#endif

  xTaskCreate(
      connectToMqtt,      /* Task function. */
      "MQTT_TASK",        /* String with name of task. */
      4096,               /* Stack size */
      NULL,               /* Parameter passed as input of the task */
      MQTT_TASK_PRIORITY, /* Priority of the task. */
      &mqttTaskHandle);   /* Task handle. */
}

void mqttStopTask()
{
  Serial.println("[MQTT] Stopping and disconnecting...");
  mqttKill = true;
  if (mqttTaskHandle != NULL)
  {
    vTaskDelete(mqttTaskHandle);
    mqttClient.disconnect();
    mqttTaskHandle = NULL;
  }
}

void sendMQTTStatus()
{
  mqttRun = true;
}

bool mqttRunning()
{
  return mqttWaiting;
}

void sendMQTTStatusTask(void *param)
{
  while (!mqttKill)
  {
    // first wait for some other tasks to settle
    vTaskDelay(SECOND * 2 / portTICK_PERIOD_MS);
    while (!mqttRun)
    {
      vTaskDelay(SECOND / portTICK_PERIOD_MS);
      continue;
    }
    Serial.println("[MQTT] sending status update");
    mqttWaiting = true;

    waitForMQTT();
    mqttSendBootStatus(bootCount, activityCount, bootReason());
    mqttSendWiFiStatus();

    mqttSendTempStatus();
    mqttSendBatteryStatus();

    mqttWaiting = false;
    mqttRun = false;
    printDebugStackSpace();
  }
  Serial.println("[MQTT] killing status task");
  vTaskDelete(NULL); // end self task
}

void startMQTTStatusTask()
{
  mqttWaiting = true;
  mqttRun = true;
  mqttKill = false;

  xTaskCreate(
      sendMQTTStatusTask,      /* Task function. */
      "MQTT_STAT_TASK",        /* String with name of task. */
      4096,                    /* Stack size */
      NULL,                    /* Parameter passed as input of the task */
      MQTT_SEND_TASK_PRIORITY, /* Priority of the task. */
      NULL);                   /* Task handle. */
}
