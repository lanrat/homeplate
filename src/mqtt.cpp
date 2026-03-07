#include <AsyncMqttClient.h>
#include <ArduinoJson.h>
#include "homeplate.h"

#define MQTT_TASK_PRIORITY 3
#define MQTT_SEND_TASK_PRIORITY 5

// Runtime MQTT topic buffers
static char mqttActionTopic[128];
static char topicBuf[128]; // shared buffer for topic construction
static char uidBuf[64];    // shared buffer for unique ID construction

static const char *mqttBaseSensor(const char *topic)
{
  snprintf(topicBuf, sizeof(topicBuf), "%s/sensor/%s/%s",
           MQTT_DISCOVERY_TOPIC, plateCfg.mqttNodeId, topic);
  return topicBuf;
}

static const char *mqttUniqueId(const char *id)
{
  snprintf(uidBuf, sizeof(uidBuf), "%s_%s", plateCfg.mqttNodeId, id);
  return uidBuf;
}

AsyncMqttClient mqttClient;
xTaskHandle mqttTaskHandle;
bool mqttFailed = false;
JsonDocument mqtt_filter;

static bool mqttWaiting; // is MQTT waiting for status to be sent
static bool mqttRun;     // should another MQTT status update be sent
static bool mqttKill;    // should the status task stop running

// State topic buffers (populated at startup)
static char state_topic_wifi_signal[128];
static char state_topic_temperature[128];
static char state_topic_battery[128];
static char state_topic_boot[128];
static char state_topic_low_battery_alert[128];

static void initMqttTopics()
{
  snprintf(mqttActionTopic, sizeof(mqttActionTopic),
           "homeplate/%s/activity/run", plateCfg.mqttNodeId);

  snprintf(state_topic_wifi_signal, sizeof(state_topic_wifi_signal),
           "%s/sensor/%s/wifi_signal/state", MQTT_DISCOVERY_TOPIC, plateCfg.mqttNodeId);
  snprintf(state_topic_temperature, sizeof(state_topic_temperature),
           "%s/sensor/%s/temperature/state", MQTT_DISCOVERY_TOPIC, plateCfg.mqttNodeId);
  snprintf(state_topic_battery, sizeof(state_topic_battery),
           "%s/sensor/%s/battery/state", MQTT_DISCOVERY_TOPIC, plateCfg.mqttNodeId);
  snprintf(state_topic_boot, sizeof(state_topic_boot),
           "%s/sensor/%s/boot/state", MQTT_DISCOVERY_TOPIC, plateCfg.mqttNodeId);
  snprintf(state_topic_low_battery_alert, sizeof(state_topic_low_battery_alert),
           "%s/sensor/%s/low_battery_alert/state", MQTT_DISCOVERY_TOPIC, plateCfg.mqttNodeId);
}

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

  JsonDocument doc;
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
  JsonDocument doc;
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
  JsonDocument doc;
  doc["voltage"] = voltage;
  doc["battery"] = percent;
  serializeJson(doc, buff);
  Serial.printf("[MQTT] Sending MQTT State: [%s] %s\n", state_topic_battery, buff);
  mqttClient.publish(state_topic_battery, 1, MQTT_RETAIN_SENSOR_VALUE, buff);
}

void mqttSendBootStatus(uint boot, uint activityCount, const char *bootReason, uint sleepDuration)
{
  char buff[512];
  JsonDocument doc;
  doc["boot"] = boot;
  doc["activity_count"] = activityCount;
  doc["boot_reason"] = bootReason;
  doc["sleep_duration"] = sleepDuration;
  serializeJson(doc, buff);
  Serial.printf("[MQTT] Sending MQTT State: [%s] %s\n", state_topic_boot, buff);
  mqttClient.publish(state_topic_boot, 1, MQTT_RETAIN_SENSOR_VALUE, buff);
}

void mqttSendLowBatteryAlert(double voltage)
{
  JsonDocument doc;
  int percent = getBatteryPercent(voltage);
  doc["voltage"] = voltage;
  doc["battery"] = percent;
  doc["alert"] = "critical_low_battery";

  char buff[256];
  serializeJson(doc, buff);
  Serial.printf("[MQTT] Sending Low Battery Alert: [%s] %s\n", state_topic_low_battery_alert, buff);
  mqttClient.publish(state_topic_low_battery_alert, 2, true, buff);
}

void sendHAConfig()
{
  Serial.println("[MQTT] Sending MQTT Config");
  const bool retain = true;
  const int qos = 1;
  char buff[768];
  char configTopic[128];
  JsonDocument doc;
  uint32_t expireAfter = getMqttExpireAfterSec();

  // macaddr
  char macaddr[18];
  String mac_string = WiFi.macAddress();
  strncpy(macaddr, mac_string.c_str(), 17);
  macaddr[17] = '\0';

  // deviceinfo
  JsonDocument deviceInfo;
  deviceInfo.clear();
  deviceInfo["manufacturer"] = "e-radionica";
  deviceInfo["model"] = DEVICE_MODEL;
  deviceInfo["name"] = plateCfg.mqttDeviceName;
  deviceInfo["sw_version"] = VERSION;
  deviceInfo["identifiers"][0] = plateCfg.mqttNodeId;
  deviceInfo["identifiers"][1] = macaddr;

  // wifi RSSI
  doc.clear();
  doc["unique_id"] = mqttUniqueId("wifi_signal");
  doc["device_class"] = "signal_strength";
  doc["state_class"] = "measurement";
  doc["name"] = "WiFi Signal";
  doc["state_topic"] = state_topic_wifi_signal;
  doc["unit_of_measurement"] = "dBm";
  doc["value_template"] = "{{ value_json.signal }}";
  doc["expire_after"] = expireAfter;
  doc["entity_category"] = "diagnostic";
  doc["device"] = deviceInfo;
  serializeJson(doc, buff);
  snprintf(configTopic, sizeof(configTopic), "%s/config", mqttBaseSensor("wifi_signal"));
  mqttClient.publish(configTopic, qos, retain, buff);

  // temp
  doc.clear();
  doc["unique_id"] = mqttUniqueId("temperature");
  doc["device_class"] = "temperature";
  doc["state_class"] = "measurement";
  doc["name"] = "Temperature";
  doc["state_topic"] = state_topic_temperature;
  doc["unit_of_measurement"] = "°C";
  doc["value_template"] = "{{ value_json.temperature }}";
  doc["expire_after"] = expireAfter;
  doc["device"] = deviceInfo;
  serializeJson(doc, buff);
  snprintf(configTopic, sizeof(configTopic), "%s/config", mqttBaseSensor("temperature"));
  mqttClient.publish(configTopic, qos, retain, buff);

  // voltage
  doc.clear();
  doc["unique_id"] = mqttUniqueId("voltage");
  doc["device_class"] = "voltage";
  doc["state_class"] = "measurement";
  doc["name"] = "Voltage";
  doc["state_topic"] = state_topic_battery;
  doc["unit_of_measurement"] = "V";
  doc["value_template"] = "{{ value_json.voltage }}";
  doc["expire_after"] = expireAfter;
  doc["device"] = deviceInfo;
  serializeJson(doc, buff);
  snprintf(configTopic, sizeof(configTopic), "%s/config", mqttBaseSensor("voltage"));
  mqttClient.publish(configTopic, qos, retain, buff);

  // battery
  doc.clear();
  doc["unique_id"] = mqttUniqueId("battery");
  doc["device_class"] = "battery";
  doc["state_class"] = "measurement";
  doc["name"] = "Battery";
  doc["state_topic"] = state_topic_battery;
  doc["unit_of_measurement"] = "%";
  doc["value_template"] = "{{ value_json.battery }}";
  doc["expire_after"] = expireAfter;
  doc["device"] = deviceInfo;
  serializeJson(doc, buff);
  snprintf(configTopic, sizeof(configTopic), "%s/config", mqttBaseSensor("battery"));
  mqttClient.publish(configTopic, qos, retain, buff);

  // boot
  doc.clear();
  doc["unique_id"] = mqttUniqueId("boot");
  doc["state_class"] = "total_increasing";
  doc["name"] = "Boot Count";
  doc["state_topic"] = state_topic_boot;
  doc["unit_of_measurement"] = "boot";
  doc["icon"] = "mdi:chart-line-variant";
  doc["value_template"] = "{{ value_json.boot }}";
  doc["expire_after"] = expireAfter;
  doc["entity_category"] = "diagnostic";
  doc["enabled_by_default"] = false;
  doc["device"] = deviceInfo;
  serializeJson(doc, buff);
  snprintf(configTopic, sizeof(configTopic), "%s/config", mqttBaseSensor("boot"));
  mqttClient.publish(configTopic, qos, retain, buff);

  // boot reason
  doc.clear();
  doc["unique_id"] = mqttUniqueId("boot_reason");
  doc["name"] = "Boot Reason";
  doc["state_topic"] = state_topic_boot;
  doc["value_template"] = "{{ value_json.boot_reason }}";
  doc["expire_after"] = expireAfter;
  doc["entity_category"] = "diagnostic";
  doc["enabled_by_default"] = false;
  doc["device"] = deviceInfo;
  serializeJson(doc, buff);
  snprintf(configTopic, sizeof(configTopic), "%s/config", mqttBaseSensor("boot_reason"));
  mqttClient.publish(configTopic, qos, retain, buff);

  // activityCount
  doc.clear();
  doc["unique_id"] = mqttUniqueId("activity_count");
  doc["state_class"] = "total_increasing";
  doc["name"] = "Activity Count";
  doc["state_topic"] = state_topic_boot;
  doc["unit_of_measurement"] = "activities";
  doc["icon"] = "mdi:chart-line-variant";
  doc["value_template"] = "{{ value_json.activity_count }}";
  doc["expire_after"] = expireAfter;
  doc["entity_category"] = "diagnostic";
  doc["enabled_by_default"] = false;
  doc["device"] = deviceInfo;
  serializeJson(doc, buff);
  snprintf(configTopic, sizeof(configTopic), "%s/config", mqttBaseSensor("activity_count"));
  mqttClient.publish(configTopic, qos, retain, buff);

  // sleepTime
  doc.clear();
  doc["unique_id"] = mqttUniqueId("sleep_duration");
  doc["state_class"] = "measurement";
  doc["name"] = "Sleep Duration";
  doc["state_topic"] = state_topic_boot;
  doc["unit_of_measurement"] = "s";
  doc["icon"] = "mdi:power-sleep";
  doc["value_template"] = "{{ value_json.sleep_duration }}";
  doc["expire_after"] = expireAfter;
  doc["entity_category"] = "diagnostic";
  doc["enabled_by_default"] = false;
  doc["device"] = deviceInfo;
  serializeJson(doc, buff);
  snprintf(configTopic, sizeof(configTopic), "%s/config", mqttBaseSensor("sleep_duration"));
  mqttClient.publish(configTopic, qos, retain, buff);

  // low battery alert
  doc.clear();
  doc["unique_id"] = mqttUniqueId("low_battery_alert");
  doc["name"] = "Low Battery Alert";
  doc["state_topic"] = state_topic_low_battery_alert;
  doc["value_template"] = "{{ value_json.alert }}";
  doc["json_attributes_topic"] = state_topic_low_battery_alert;
  doc["icon"] = "mdi:battery-alert";
  doc["entity_category"] = "diagnostic";
  doc["device"] = deviceInfo;
  serializeJson(doc, buff);
  snprintf(configTopic, sizeof(configTopic), "%s/config", mqttBaseSensor("low_battery_alert"));
  mqttClient.publish(configTopic, qos, retain, buff);
}

void connectToMqtt(void *params)
{
  while (true)
  {
    printDebug("[MQTT] MQTT loop...");
    if (mqttClient.connected())
    {
      vTaskDelay(10000 / portTICK_PERIOD_MS);
      continue;
    }

    waitForWiFi();
    Serial.println("[MQTT] Connecting to MQTT...");
    mqttClient.connect();

    unsigned long startAttemptTime = millis();
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

  uint16_t packetIdSub = mqttClient.subscribe(mqttActionTopic, 2);
  Serial.print("[MQTT] Subscribing at QoS 2, packetId: ");
  Serial.println(packetIdSub);

  if (!sleepBoot || (bootCount % MQTT_RESEND_CONFIG_EVERY) == 0)
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
  if (!topic || !payload) {
    Serial.println("[MQTT] Invalid message: null topic or payload");
    return;
  }

  const size_t MAX_MQTT_PAYLOAD_SIZE = 8192;
  if (len > MAX_MQTT_PAYLOAD_SIZE) {
    Serial.printf("[MQTT] Payload too large: %zu bytes (max %zu)\n", len, MAX_MQTT_PAYLOAD_SIZE);
    return;
  }

  Serial.println("[MQTT] Publish received.");
  Serial.print("  topic: ");
  Serial.println(topic);
  Serial.print("  qos: ");
  Serial.println(properties.qos);
  Serial.print("  retain: ");
  Serial.println(properties.retain);
  Serial.print("  len: ");
  Serial.println(len);

  if (strncmp(topic, mqttActionTopic, strlen(mqttActionTopic)) != 0)
  {
    return;
  }

  if (len > 0)
  {
    mqttClient.publish(mqttActionTopic, 1, true);

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, len, DeserializationOption::Filter(mqtt_filter));
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

    if (doc["refresh"].is<JsonVariant>())
    {
      int refresh = doc["refresh"].as<int>();
      Serial.printf("[MQTT][REFRESH]: %d\n", refresh);
      if (refresh > 0 && refresh < MAX_REFRESH_SEC)
      {
        setSleepDuration((uint32_t) refresh);
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
    else if (strncmp("trmnl", action, 6) == 0)
    {
      startActivity(Trmnl);
      return;
    }
    else if (strncmp("message", action, 9) == 0)
    {
      if (!doc["message"].is<JsonVariant>())
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
      if (!doc["message"].is<JsonVariant>())
      {
        Serial.printf("[MQTT][ERROR] img action has no url message!\n");
        return;
      }
      setMessage(doc["message"]);
      startActivity(IMG);
      return;
    }
    Serial.printf("[MQTT][ERROR] unable to handle action %s\n", action);
  }
}

void startMQTTTask()
{
  if (mqttTaskHandle != NULL)
  {
    Serial.printf("[MQTT] MQTT Task Already running\n");
    return;
  }

  initMqttTopics();

  Serial.printf("[MQTT] starting MQTT\n");
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);

  mqtt_filter["action"] = true;
  mqtt_filter["message"] = true;
  mqtt_filter["refresh"] = true;

  mqttClient.setClientId(plateCfg.hostname);
  mqttClient.setServer(plateCfg.mqttHost, plateCfg.mqttPort);
  if (strlen(plateCfg.mqttUser) > 0)
  {
    mqttClient.setCredentials(plateCfg.mqttUser, plateCfg.mqttPassword);
  }

  xTaskCreate(
      connectToMqtt,
      "MQTT_TASK",
      4096,
      NULL,
      MQTT_TASK_PRIORITY,
      &mqttTaskHandle);
}

void mqttStopTask()
{
  mqttKill = true;
  if (mqttTaskHandle != NULL)
  {
    Serial.println("[MQTT] Stopping and disconnecting...");
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
    vTaskDelay(SECOND * 2 / portTICK_PERIOD_MS);
    while (!mqttRun)
    {
      vTaskDelay(SECOND / portTICK_PERIOD_MS);
      continue;
    }
    Serial.println("[MQTT] sending status update");
    mqttWaiting = true;

    waitForMQTT();
    mqttSendBootStatus(bootCount, activityCount, bootReason(), timeToSleep);
    mqttSendWiFiStatus();
    mqttSendTempStatus();
    mqttSendBatteryStatus();

    mqttWaiting = false;
    mqttRun = false;
    printDebugStackSpace();
  }
  Serial.println("[MQTT] killing status task");
  vTaskDelete(NULL);
}

void startMQTTStatusTask()
{
  mqttWaiting = true;
  mqttRun = false;
  mqttKill = false;

  if (strlen(plateCfg.mqttHost) == 0)
  {
    Serial.println("[MQTT] not starting status task, MQTT host not configured");
    mqttFailed = true;
    return;
  }

  xTaskCreate(
      sendMQTTStatusTask,
      "MQTT_STAT_TASK",
      4096,
      NULL,
      MQTT_SEND_TASK_PRIORITY,
      NULL);
}
