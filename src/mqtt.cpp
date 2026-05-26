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
#ifdef INKPLATE_HAS_TEMPERATURE
static char state_topic_temperature[128];
#endif
static char state_topic_battery[128];
static char state_topic_boot[128];
static char state_topic_low_battery_alert[128];
static char topic_dither_options[128];

// Forward decls for config-entity helpers defined further below.
static char btnRebootTopic[128];
static char btnSetupTopic[128];
static void sendConfigDiscovery(JsonDocument &deviceInfo, char *buff, size_t buffSz);

static void initMqttTopics()
{
  snprintf(mqttActionTopic, sizeof(mqttActionTopic),
           "homeplate/%s/activity/run", plateCfg.mqttNodeId);
  snprintf(state_topic_wifi_signal, sizeof(state_topic_wifi_signal),
           "%s/sensor/%s/wifi_signal/state", MQTT_DISCOVERY_TOPIC, plateCfg.mqttNodeId);
#ifdef INKPLATE_HAS_TEMPERATURE
  snprintf(state_topic_temperature, sizeof(state_topic_temperature),
           "%s/sensor/%s/temperature/state", MQTT_DISCOVERY_TOPIC, plateCfg.mqttNodeId);
#endif
  snprintf(state_topic_battery, sizeof(state_topic_battery),
           "%s/sensor/%s/battery/state", MQTT_DISCOVERY_TOPIC, plateCfg.mqttNodeId);
  snprintf(state_topic_boot, sizeof(state_topic_boot),
           "%s/sensor/%s/boot/state", MQTT_DISCOVERY_TOPIC, plateCfg.mqttNodeId);
  snprintf(state_topic_low_battery_alert, sizeof(state_topic_low_battery_alert),
           "%s/sensor/%s/low_battery_alert/state", MQTT_DISCOVERY_TOPIC, plateCfg.mqttNodeId);
  snprintf(topic_dither_options, sizeof(topic_dither_options),
           "homeplate/%s/dither/options", plateCfg.mqttNodeId);
  snprintf(btnRebootTopic, sizeof(btnRebootTopic),
           "homeplate/%s/cmd/reboot", plateCfg.mqttNodeId);
  snprintf(btnSetupTopic, sizeof(btnSetupTopic),
           "homeplate/%s/cmd/setup_mode", plateCfg.mqttNodeId);
}

static void mqttPublishDitherOptions()
{
  char buff[256];
  size_t n = buildDitherOptionsJson(buff, sizeof(buff));
  if (n == 0) {
    Serial.println("[MQTT][ERROR] dither options JSON overflow");
    return;
  }
  Serial.printf("[MQTT] Sending dither options: [%s] %s\n", topic_dither_options, buff);
  mqttClient.publish(topic_dither_options, 1, true, buff);
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

#ifdef INKPLATE_HAS_TEMPERATURE
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
#endif

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
  doc["enabled_by_default"] = false;
  doc["device"] = deviceInfo;
  serializeJson(doc, buff);
  snprintf(configTopic, sizeof(configTopic), "%s/config", mqttBaseSensor("wifi_signal"));
  mqttClient.publish(configTopic, qos, retain, buff);

#ifdef INKPLATE_HAS_TEMPERATURE
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
#endif

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
  doc["entity_category"] = "diagnostic";
  doc["enabled_by_default"] = false;
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
  doc["enabled_by_default"] = false;
  doc["device"] = deviceInfo;
  serializeJson(doc, buff);
  snprintf(configTopic, sizeof(configTopic), "%s/config", mqttBaseSensor("low_battery_alert"));
  mqttClient.publish(configTopic, qos, retain, buff);

  // Config entities (number/select/text/switch) + reboot/setup buttons
  sendConfigDiscovery(deviceInfo, buff, sizeof(buff));
}

// ============================================================================
// Configuration entities exposed via HA MQTT Discovery
// ----------------------------------------------------------------------------
// Each ConfigEntity row describes one user-mutable setting. The table drives
// discovery publish, state publish, subscribe, and command dispatch — adding a
// new exposed setting only requires appending a row here (plus, if it's a new
// type, extending the small switch statements below).
//
// Topics:
//   discovery: <D>/<component>/<node>/cfg_<key>/config   (retained)
//   state:     homeplate/<node>/config/<key>/state       (retained, raw scalar)
//   command:   homeplate/<node>/config/<key>/set         (subscribed)
//
// Command idempotency: every handler dirty-checks against the live value
// before writing NVS, so retained replays on reconnect (or external retained
// publishes) are no-ops.
// ============================================================================

enum HAComp { HC_NUMBER, HC_SELECT, HC_TEXT, HC_TEXT_PASSWORD, HC_SWITCH };
enum CType  { CT_U16, CT_BOOL, CT_STR, CT_ENUM_STR, CT_DITHER };

struct ConfigEntity {
  const char *key;             // NVS key + topic slug
  const char *name;            // HA friendly name
  const char *icon;            // mdi:..., may be nullptr
  HAComp comp;
  CType  type;
  void  *valPtr;               // pointer into plateCfg
  size_t valSize;              // for strings (includes NUL)
  int32_t numMin, numMax;      // for HC_NUMBER
  const char *unit;            // for HC_NUMBER, may be nullptr
  const char *const *options;  // null-terminated for CT_ENUM_STR
  bool sensitive;              // mask in serial logs
  void (*applyFn)();           // optional post-write hook
  bool diagnostic;             // entity_category=diagnostic instead of config
};

static const char *const activityOpts[] = {"HomeAssistant", "Trmnl", "Info", "GuestWifi", nullptr};

static const ConfigEntity configEntities[] = {
  // key            name                   icon                            comp              type         valPtr                            valSize                                  min max    unit   options       sens   applyFn        diag
  {"sleep_min",    "Sleep Minutes",       "mdi:timer-sand",               HC_NUMBER,        CT_U16,      &plateCfg.sleepMinutes,           0,                                       1, 1440,  "min", nullptr,      false, nullptr,       false},
  {"quick_sleep",  "Quick Sleep Seconds", "mdi:timer",                    HC_NUMBER,        CT_U16,      &plateCfg.quickSleepSec,          0,                                       0, 86400, "s",   nullptr,      false, nullptr,       false},
  {"def_activity", "Default Activity",    "mdi:application",              HC_SELECT,        CT_ENUM_STR, plateCfg.defaultActivityStr,      sizeof(plateCfg.defaultActivityStr),     0, 0,     nullptr, activityOpts, false, nullptr,       false},
  {"image_url",    "Image URL",           "mdi:link-variant",             HC_TEXT,          CT_STR,      plateCfg.imageUrl,                sizeof(plateCfg.imageUrl),               0, 0,     nullptr, nullptr,      false, nullptr,       false},
  {"trmnl_url",    "TRMNL URL",           "mdi:link-variant",             HC_TEXT,          CT_STR,      plateCfg.trmnlUrl,                sizeof(plateCfg.trmnlUrl),               0, 0,     nullptr, nullptr,      false, nullptr,       false},
  {"trmnl_id",     "TRMNL ID",            "mdi:identifier",               HC_TEXT,          CT_STR,      plateCfg.trmnlId,                 sizeof(plateCfg.trmnlId),                0, 0,     nullptr, nullptr,      false, nullptr,       false},
  {"trmnl_token",  "TRMNL Token",         "mdi:key",                      HC_TEXT_PASSWORD, CT_STR,      plateCfg.trmnlToken,              sizeof(plateCfg.trmnlToken),             0, 0,     nullptr, nullptr,      true,  nullptr,       false},
  {"trmnl_log",    "TRMNL Logging",       "mdi:text-box-outline",         HC_SWITCH,        CT_BOOL,     &plateCfg.trmnlEnableLog,         0,                                       0, 0,     nullptr, nullptr,      false, nullptr,       false},
  {"dither_kern",  "Dither Kernel",       "mdi:image-filter-black-white", HC_SELECT,        CT_DITHER,   &plateCfg.ditherKernel,           0,                                       0, 0,     nullptr, nullptr,      false, nullptr,       false},
  {"disp_time",    "Show Update Time",    "mdi:clock-outline",            HC_SWITCH,        CT_BOOL,     &plateCfg.displayLastUpdateTime,  0,                                       0, 0,     nullptr, nullptr,      false, nullptr,       true},
  {"timezone",     "Timezone (POSIX TZ)", "mdi:earth",                    HC_TEXT,          CT_STR,      plateCfg.timezone,                sizeof(plateCfg.timezone),               0, 0,     nullptr, nullptr,      false, applyTimezone, false},
  {"qr_name",      "Guest WiFi SSID",     "mdi:wifi",                     HC_TEXT,          CT_STR,      plateCfg.qrWifiName,              sizeof(plateCfg.qrWifiName),             0, 0,     nullptr, nullptr,      false, nullptr,       false},
  {"qr_pass",      "Guest WiFi Password", "mdi:wifi-lock",                HC_TEXT_PASSWORD, CT_STR,      plateCfg.qrWifiPassword,          sizeof(plateCfg.qrWifiPassword),         0, 0,     nullptr, nullptr,      true,  nullptr,       false},
};
static constexpr size_t configEntitiesCount = sizeof(configEntities) / sizeof(configEntities[0]);

static const char *haComponentStr(HAComp c)
{
  switch (c)
  {
  case HC_NUMBER:        return "number";
  case HC_SELECT:        return "select";
  case HC_TEXT:
  case HC_TEXT_PASSWORD: return "text";
  case HC_SWITCH:        return "switch";
  }
  return "";
}

static void cfgStateTopic(char *buf, size_t sz, const char *key)
{
  snprintf(buf, sz, "homeplate/%s/config/%s/state", plateCfg.mqttNodeId, key);
}
static void cfgCmdTopic(char *buf, size_t sz, const char *key)
{
  snprintf(buf, sz, "homeplate/%s/config/%s/set", plateCfg.mqttNodeId, key);
}
static void cfgDiscoveryTopic(char *buf, size_t sz, HAComp comp, const char *key)
{
  snprintf(buf, sz, "%s/%s/%s/cfg_%s/config", MQTT_DISCOVERY_TOPIC, haComponentStr(comp), plateCfg.mqttNodeId, key);
}

static void publishConfigState(const ConfigEntity &e)
{
  char stTopic[128];
  char payload[260];
  cfgStateTopic(stTopic, sizeof(stTopic), e.key);
  switch (e.type)
  {
  case CT_U16:
    snprintf(payload, sizeof(payload), "%u", *(uint16_t *)e.valPtr);
    break;
  case CT_BOOL:
    snprintf(payload, sizeof(payload), "%s", *(bool *)e.valPtr ? "ON" : "OFF");
    break;
  case CT_STR:
  case CT_ENUM_STR:
    strlcpy(payload, (const char *)e.valPtr, sizeof(payload));
    break;
  case CT_DITHER:
    strlcpy(payload, ditherKernelName(*(uint8_t *)e.valPtr), sizeof(payload));
    break;
  }
  if (e.sensitive)
    Serial.printf("[MQTT] Publish config state: [%s] ***(%d)\n", stTopic, (int)strlen(payload));
  else
    Serial.printf("[MQTT] Publish config state: [%s] %s\n", stTopic, payload);
  mqttClient.publish(stTopic, 1, true, payload);
}

static void publishConfigDiscovery(const ConfigEntity &e, JsonDocument &deviceInfo, char *buff, size_t buffSz)
{
  JsonDocument doc;
  char stTopic[128], cmdTopic[128], discTopic[160], uniqueId[80];
  cfgStateTopic(stTopic, sizeof(stTopic), e.key);
  cfgCmdTopic(cmdTopic, sizeof(cmdTopic), e.key);
  cfgDiscoveryTopic(discTopic, sizeof(discTopic), e.comp, e.key);
  snprintf(uniqueId, sizeof(uniqueId), "%s_cfg_%s", plateCfg.mqttNodeId, e.key);

  doc["unique_id"] = uniqueId;
  doc["name"] = e.name;
  if (e.icon) doc["icon"] = e.icon;
  doc["state_topic"] = stTopic;
  doc["command_topic"] = cmdTopic;
  doc["entity_category"] = e.diagnostic ? "diagnostic" : "config";
  doc["device"] = deviceInfo;

  switch (e.comp)
  {
  case HC_NUMBER:
    doc["min"] = e.numMin;
    doc["max"] = e.numMax;
    doc["step"] = 1;
    doc["mode"] = "box";
    if (e.unit) doc["unit_of_measurement"] = e.unit;
    break;
  case HC_SELECT:
  {
    JsonArray arr = doc["options"].to<JsonArray>();
    if (e.type == CT_DITHER)
    {
      arr.add(ditherKernelName(0)); // "none"
      for (uint8_t i = 1; i <= DITHER_KERNEL_COUNT; i++)
        arr.add(ditherKernelName(i));
    }
    else if (e.options)
    {
      for (const char *const *p = e.options; *p; p++)
        arr.add(*p);
    }
    break;
  }
  case HC_TEXT:
    doc["min"] = 0;
    doc["max"] = (uint32_t)(e.valSize - 1);
    doc["mode"] = "text";
    break;
  case HC_TEXT_PASSWORD:
    doc["min"] = 0;
    doc["max"] = (uint32_t)(e.valSize - 1);
    doc["mode"] = "password";
    break;
  case HC_SWITCH:
    doc["payload_on"]  = "ON";
    doc["payload_off"] = "OFF";
    doc["state_on"]    = "ON";
    doc["state_off"]   = "OFF";
    break;
  }
  serializeJson(doc, buff, buffSz);
  mqttClient.publish(discTopic, 1, true, buff);
}

static void publishConfigButtons(JsonDocument &deviceInfo, char *buff, size_t buffSz)
{
  // Reboot button
  {
    JsonDocument doc;
    char uniqueId[80], discTopic[160];
    snprintf(uniqueId, sizeof(uniqueId), "%s_cmd_reboot", plateCfg.mqttNodeId);
    snprintf(discTopic, sizeof(discTopic), "%s/button/%s/cmd_reboot/config", MQTT_DISCOVERY_TOPIC, plateCfg.mqttNodeId);
    doc["unique_id"] = uniqueId;
    doc["name"] = "Reboot";
    doc["command_topic"] = btnRebootTopic;
    doc["payload_press"] = "PRESS";
    doc["device_class"] = "restart";
    doc["entity_category"] = "diagnostic";
    doc["icon"] = "mdi:restart";
    doc["device"] = deviceInfo;
    serializeJson(doc, buff, buffSz);
    mqttClient.publish(discTopic, 1, true, buff);
  }
  // Setup-mode button
  {
    JsonDocument doc;
    char uniqueId[80], discTopic[160];
    snprintf(uniqueId, sizeof(uniqueId), "%s_cmd_setup_mode", plateCfg.mqttNodeId);
    snprintf(discTopic, sizeof(discTopic), "%s/button/%s/cmd_setup_mode/config", MQTT_DISCOVERY_TOPIC, plateCfg.mqttNodeId);
    doc["unique_id"] = uniqueId;
    doc["name"] = "Enter Setup Mode";
    doc["command_topic"] = btnSetupTopic;
    doc["payload_press"] = "PRESS";
    doc["entity_category"] = "config";
    doc["icon"] = "mdi:cog-play";
    doc["device"] = deviceInfo;
    serializeJson(doc, buff, buffSz);
    mqttClient.publish(discTopic, 1, true, buff);
  }
}

static void publishAllConfigStates()
{
  for (size_t i = 0; i < configEntitiesCount; i++)
    publishConfigState(configEntities[i]);
}

static void subscribeConfigCommands()
{
  char cmdTopic[128];
  for (size_t i = 0; i < configEntitiesCount; i++)
  {
    cfgCmdTopic(cmdTopic, sizeof(cmdTopic), configEntities[i].key);
    mqttClient.subscribe(cmdTopic, 1);
  }
  mqttClient.subscribe(btnRebootTopic, 1);
  mqttClient.subscribe(btnSetupTopic, 1);
}

// Return true if the topic was handled by the config command dispatcher.
static bool handleConfigCommand(const char *topic, const char *payload, size_t len)
{
  char cmdTopic[128];
  for (size_t i = 0; i < configEntitiesCount; i++)
  {
    const ConfigEntity &e = configEntities[i];
    cfgCmdTopic(cmdTopic, sizeof(cmdTopic), e.key);
    if (strcmp(topic, cmdTopic) != 0)
      continue;

    // Payload is not NUL-terminated; copy into a local buffer.
    char buf[320];
    size_t copyLen = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, payload, copyLen);
    buf[copyLen] = '\0';

    if (e.sensitive)
      Serial.printf("[MQTT][CFG] Received: %s = ***(%d)\n", e.key, (int)copyLen);
    else
      Serial.printf("[MQTT][CFG] Received: %s = %s\n", e.key, buf);

    bool dirty = false;
    bool invalid = false;
    switch (e.type)
    {
    case CT_U16:
    {
      int32_t v = atoi(buf);
      if (v < e.numMin) v = e.numMin;
      if (v > e.numMax) v = e.numMax;
      uint16_t *p = (uint16_t *)e.valPtr;
      if (*p != (uint16_t)v) { *p = (uint16_t)v; dirty = true; }
      break;
    }
    case CT_BOOL:
    {
      bool v = (strcasecmp(buf, "ON") == 0 || strcasecmp(buf, "true") == 0 || strcasecmp(buf, "1") == 0);
      bool *p = (bool *)e.valPtr;
      if (*p != v) { *p = v; dirty = true; }
      break;
    }
    case CT_ENUM_STR:
    {
      bool valid = false;
      for (const char *const *q = e.options; q && *q; q++)
        if (strcmp(buf, *q) == 0) { valid = true; break; }
      if (!valid) { invalid = true; break; }
      char *p = (char *)e.valPtr;
      if (strcmp(p, buf) != 0) { strlcpy(p, buf, e.valSize); dirty = true; }
      break;
    }
    case CT_STR:
    {
      char *p = (char *)e.valPtr;
      if (strcmp(p, buf) != 0) { strlcpy(p, buf, e.valSize); dirty = true; }
      break;
    }
    case CT_DITHER:
    {
      int8_t v = parseDitherName(buf);
      if (v < 0)
      {
        // also accept a numeric index
        char *endp = nullptr;
        long n = strtol(buf, &endp, 10);
        if (endp != buf && *endp == '\0' && n >= 0 && n <= DITHER_KERNEL_COUNT)
          v = (int8_t)n;
      }
      if (v < 0) { invalid = true; break; }
      uint8_t *p = (uint8_t *)e.valPtr;
      if (*p != (uint8_t)v) { *p = (uint8_t)v; dirty = true; }
      break;
    }
    }

    if (invalid)
    {
      Serial.printf("[MQTT][CFG][ERROR] invalid value for %s\n", e.key);
    }
    else if (dirty)
    {
      Serial.printf("[MQTT][CFG] %s changed, persisting to NVS\n", e.key);
      if (e.applyFn) e.applyFn();
      saveConfig();
    }
    else
    {
      Serial.printf("[MQTT][CFG] %s unchanged, skipping NVS write\n", e.key);
    }
    publishConfigState(e); // always echo current value
    return true;
  }
  return false;
}

static bool handleCmdButton(const char *topic, const char *payload, size_t len)
{
  if (strcmp(topic, btnRebootTopic) == 0)
  {
    Serial.println("[MQTT][CMD] Reboot requested via MQTT");
    vTaskDelay(500 / portTICK_PERIOD_MS);
    ESP.restart();
    return true;
  }
  if (strcmp(topic, btnSetupTopic) == 0)
  {
    Serial.println("[MQTT][CMD] Setup mode requested via MQTT");
    setForcePortalFlag(true);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    ESP.restart();
    return true;
  }
  return false;
}

static void sendConfigDiscovery(JsonDocument &deviceInfo, char *buff, size_t buffSz)
{
  for (size_t i = 0; i < configEntitiesCount; i++)
    publishConfigDiscovery(configEntities[i], deviceInfo, buff, buffSz);
  publishConfigButtons(deviceInfo, buff, buffSz);
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

  subscribeConfigCommands();

  if (!sleepBoot || (bootCount % MQTT_RESEND_CONFIG_EVERY) == 0) {
    sendHAConfig();
    mqttPublishDitherOptions();
  }

  // Always publish current config state (retained) so HA stays in sync.
  publishAllConfigStates();
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

  // Dispatch to config/button handlers first (exact topic match). These
  // accept zero-length payloads (e.g. retained-clear for buttons) and return
  // true once a topic matches.
  if (handleCmdButton(topic, payload, len))
    return;
  if (handleConfigCommand(topic, payload, len))
    return;

  if (strcmp(topic, mqttActionTopic) != 0)
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
      const char *d = doc["dither"].is<const char *>() ? doc["dither"].as<const char *>() : nullptr;
      setPendingDitherOverride(parseDitherName(d));
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
  mqtt_filter["dither"] = true;

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
#ifdef INKPLATE_HAS_TEMPERATURE
    mqttSendTempStatus();
#endif
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
