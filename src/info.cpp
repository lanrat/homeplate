#include "homeplate.h"
#include <esp_chip_info.h>

#if E_INK_WIDTH < 800
// Small displays (Inkplate 6 COLOR 600px): symmetric layout with tight margins.
// (32ths) 1 left | 6 label1 | 8 data1 | 2 middle gap | 6 label2 | 8 data2 | 1 right
#define COL1_NAME_X (1  * (E_INK_WIDTH / 32))
#define COL1_DATA_X (7  * (E_INK_WIDTH / 32))
#define COL2_NAME_X (17 * (E_INK_WIDTH / 32))
#define COL2_DATA_X (23 * (E_INK_WIDTH / 32))
#else
#define COL1_NAME_X 1 * (E_INK_WIDTH / 8)
#define COL1_DATA_X 2 * (E_INK_WIDTH / 8)
#define COL2_NAME_X 5 * (E_INK_WIDTH / 8)
#define COL2_DATA_X 6 * (E_INK_WIDTH / 8)
#endif

#define REDRAW_NETWORK 0
#define REDRAW_WIFI    1
#define REDRAW_MQTT    2

// Use font's yAdvance to prevent line overlap on smaller displays
const static int lineHeight = (int)FONT_BODY.yAdvance + 2;

// truncateToFit: returns s unchanged if it fits in max_width pixels at the
// current font, otherwise returns a pointer to a static buffer containing
// "<head>..." truncated to fit. Measures against display's current font.
// Single static buffer — not safe to nest calls in one print statement.
static const char *truncateToFit(const char *s, uint16_t max_width)
{
    static char buf[128];
    if (!s) return "";
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
    if (w <= max_width) return s;
    // Doesn't fit. Try progressively shorter "<head>..." until it does.
    for (size_t n = strlen(s); n > 0; n--)
    {
        snprintf(buf, sizeof(buf), "%.*s...", (int)n, s);
        display.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
        if (w <= max_width) return buf;
    }
    return "...";
}

const char *wl_status_to_string(wl_status_t status)
{
  switch (status)
  {
  case WL_NO_SHIELD:
    return "NO_SHIELD";
  case WL_IDLE_STATUS:
    return "IDLE_STATUS";
  case WL_NO_SSID_AVAIL:
    return "NO_SSID_AVAIL";
  case WL_SCAN_COMPLETED:
    return "SCAN_COMPLETED";
  case WL_CONNECTED:
    return "CONNECTED";
  case WL_CONNECT_FAILED:
    return "CONNECT_FAILED";
  case WL_CONNECTION_LOST:
    return "CONNECTION_LOST";
  case WL_DISCONNECTED:
    return "DISCONNECTED";
  }
  return "UNKNOWN";
}

void displayBoundaryBox()
{
  int bw = max(scaleX(10), 2);
  display.fillRect(0, 0, bw, E_INK_HEIGHT, HP_FG);                // left
  display.fillRect(E_INK_WIDTH - bw, 0, bw, E_INK_HEIGHT, HP_FG); // right
  display.fillRect(0, 0, E_INK_WIDTH, bw, HP_FG);                 // top
  display.fillRect(0, E_INK_HEIGHT - bw, E_INK_WIDTH, bw, HP_FG); // bottom
}

void cleanField(uint32_t x, uint32_t y)
{
  display.fillRect(x, y - lineHeight, (E_INK_WIDTH / 8), lineHeight, HP_BG);
  //Serial.printf("fillRect(x:%u, y:%u, w:%u, h:%u)\n", x, y, (E_INK_WIDTH / 8), lineHeight);
}

void drawNetwork(uint32_t *yref, bool clean)
{
  uint32_t y = *yref;

  // network
  display.setCursor(COL2_NAME_X, y);
  display.print("Hostname:");
  if (clean) { cleanField(COL2_DATA_X, y); };
  display.setCursor(COL2_DATA_X, y);
  display.print(WiFi.getHostname());
  // ip
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.print("IP Address:");
  if (clean) { cleanField(COL2_DATA_X, y); };
  display.setCursor(COL2_DATA_X, y);
  display.print(WiFi.localIP().toString().c_str());
  // netmask
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.print("Subnet Mask:");
  if (clean) { cleanField(COL2_DATA_X, y); };
  display.setCursor(COL2_DATA_X, y);
  display.print(WiFi.subnetMask().toString().c_str());
  // gateway
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.print("Gateway:");
  if (clean) { cleanField(COL2_DATA_X, y); };
  display.setCursor(COL2_DATA_X, y);
  display.print(WiFi.gatewayIP().toString().c_str());
  // dns
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.print("DNS:");
  if (clean) { cleanField(COL2_DATA_X, y); };
  display.setCursor(COL2_DATA_X, y);
  display.print(WiFi.dnsIP(0).toString().c_str());
  // mac
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.print("MAC Address:");
  if (clean) { cleanField(COL2_DATA_X, y); };
  display.setCursor(COL2_DATA_X, y);
  display.print(WiFi.macAddress().c_str());

  *yref = y;
}

void drawWiFi(uint32_t *yref, bool clean)
{
  uint32_t y = *yref;

  display.setCursor(COL2_NAME_X, y);
  display.print("SSID:");
  if (clean) { cleanField(COL2_DATA_X, y); };
  display.setCursor(COL2_DATA_X, y);
  display.print(WiFi.SSID().c_str());
  // bssid
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.print("BSSID:");
  if (clean) { cleanField(COL2_DATA_X, y); };
  display.setCursor(COL2_DATA_X, y);
  display.print(WiFi.BSSIDstr().c_str());
  // signal
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.print("Signal:");
  if (clean) { cleanField(COL2_DATA_X, y); };
  display.setCursor(COL2_DATA_X, y);
  display.printf("%d dBm", WiFi.RSSI());
  // wifi status
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.print("WiFi Status:");
  if (clean) { cleanField(COL2_DATA_X, y); };
  display.setCursor(COL2_DATA_X, y);
  display.print(wl_status_to_string(WiFi.status()));

  *yref = y;
}

void drawMQTT(uint32_t *yref, bool clean)
{
  uint32_t y = *yref;

  display.setCursor(COL2_NAME_X, y);
  display.printf("MQTT Server:");
  if (clean) { cleanField(COL2_DATA_X, y); };
  display.setCursor(COL2_DATA_X, y);
  display.print(plateCfg.mqttHost);
  // MQTT status
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.printf("MQTT Status:");
  if (clean) { cleanField(COL2_DATA_X, y); };
  display.setCursor(COL2_DATA_X, y);
  display.printf(mqttConnected() ? "OK" : "Error");

  *yref = y;
}

void displayInfoScreen()
{
  Serial.println("[INFO] Rendering info page");
  static char buff[1024];
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);

  i2cStart();
  displayStart();
#ifdef INKPLATE_HAS_DISPLAY_MODES
  display.selectDisplayMode(INKPLATE_1BIT);
#endif
  display.setTextColor(HP_FG, HP_BG);
  display.clearDisplay();

  // Title
  display.setFont(&FONT_HEADING);
  display.setTextSize(1);
  uint32_t y = centerTextX("HomePlate Info", 0, E_INK_WIDTH, scaleY(100), false);
  display.setFont(&FONT_BODY);
  // version
  snprintf(buff, 1024, "Version: [%s]", VERSION);
  y = centerTextX(buff, 0, E_INK_WIDTH, y + scaleY(110), false);

  // column 1
  // Model
  y = scaleY(250);
  display.setCursor(COL1_NAME_X, y);
  display.print("Model:");
  display.setCursor(COL1_DATA_X, y);
  display.print(DEVICE_MODEL);
  // CPU
  y += lineHeight;
  display.setCursor(COL1_NAME_X, y);
  display.print("CPU:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%u core(s)", chip_info.cores);
  // frequency
  y += lineHeight;
  display.setCursor(COL1_NAME_X, y);
  display.print("Frequency:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%u Mhz", getCpuFrequencyMhz());
  // Features
  y += lineHeight;
  display.setCursor(COL1_NAME_X, y);
  display.print("Features:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%s%s%s", (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi" : "", (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "", (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");
  // Flash
  y += lineHeight;
  display.setCursor(COL1_NAME_X, y);
  display.print("Flash:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%dMB %s", ESP.getFlashChipSize() / (1024 * 1024), (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
  // Heap
  y += lineHeight;
  display.setCursor(COL1_NAME_X, y);
  display.print("Min Heap Free:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%.2fMB", (esp_get_minimum_free_heap_size()) / (1024.0F * 1024.0F));
  // Tasks
  y += lineHeight;
  display.setCursor(COL1_NAME_X, y);
  display.print("Tasks:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%d", uxTaskGetNumberOfTasks());
  // Display
  y += lineHeight;
  display.setCursor(COL1_NAME_X, y);
  display.print("Display:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%dx%d", E_INK_WIDTH, E_INK_HEIGHT);
  // Dither
  y += lineHeight;
  display.setCursor(COL1_NAME_X, y);
  display.print("Dither:");
  display.setCursor(COL1_DATA_X, y);
  display.print(ditherKernelName(plateCfg.ditherKernel));

  // bootCount
  y += lineHeight * 2;
  display.setCursor(COL1_NAME_X, y);
  display.print("Boot #:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%u", bootCount);
  // loopCount
  y += lineHeight;
  display.setCursor(COL1_NAME_X, y);
  display.print("Activity #:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%u", activityCount);
  // wake reason
  y += lineHeight;
  display.setCursor(COL1_NAME_X, y);
  display.print("Wake Reason:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%s", bootReason());
  // battery
  y += lineHeight;
  double voltage = display.readBattery();
  int percent = getBatteryPercent(voltage);
  display.setCursor(COL1_NAME_X, y);
  display.print("Battery:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%d%% (%.2fv)", percent, voltage);
#ifdef INKPLATE_HAS_TEMPERATURE
  // temp
  y += lineHeight;
  int temp = display.readTemperature();
  int tempF = (temp * 9 / 5) + 32;
  display.setCursor(COL1_NAME_X, y);
  display.print("Temperature:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%dC (%dF)", temp, tempF);
#endif

  if (strlen(plateCfg.trmnlId) > 0) {
  // TRMNL
  y += lineHeight * 2;
  display.setCursor(COL1_NAME_X, y);
  display.print("TRMNL ID:");
  display.setCursor(COL1_DATA_X, y);
  display.print(plateCfg.trmnlId);
  y += lineHeight;
  display.setCursor(COL1_NAME_X, y);
  display.print("TRMNL URL:");
  display.setCursor(COL1_DATA_X, y);
  // Truncate to stay within the left column — otherwise on small displays
  // the URL bleeds rightward into the right column's data.
  display.print(truncateToFit(plateCfg.trmnlUrl, COL2_NAME_X - COL1_DATA_X - scaleX(20)));
  }

  // column 2
  y = scaleY(250);
  // network
  uint32_t yNetwork = y;
  drawNetwork(&y, false);

  // ssid
  y += lineHeight * 2;
  uint32_t yWiFi = y;
  bool stateWiFi = WiFi.isConnected();
  drawWiFi(&y, false);

  bool stateMQTT = true;
  uint32_t yMQTT = 0;
  if (strlen(plateCfg.mqttHost) > 0) {
  // MQTT server
  y += lineHeight * 2;
  yMQTT = y;
  stateMQTT = mqttConnected();
  drawMQTT(&y, false);
  }

  if (strlen(plateCfg.ntpServer) > 0) {
  // time
  y += lineHeight * 2;
  display.setCursor(COL2_NAME_X, y);
  display.print("Time:");
  display.setCursor(COL2_DATA_X, y);
  display.print(shortDateTimeString());
  // Timezone
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.print("Timezone:");
  display.setCursor(COL2_DATA_X, y);
  display.print(plateCfg.timezone);
  // NTP
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.print("NTP Server:");
  display.setCursor(COL2_DATA_X, y);
  display.print(plateCfg.ntpServer);
  // NTP status
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.print("NTP Status:");
  display.setCursor(COL2_DATA_X, y);
  display.print(getNTPSynced() ? "OK" : "False");

  // RTC
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.printf("Ext RTC:");
  display.setCursor(COL2_DATA_X, y);
  display.printf(display.rtc.isSet() ? "OK" : "Error");
  }

  displayBoundaryBox();
  displayRefresh();
  displayEnd();
  i2cEnd();

#ifdef INKPLATE_HAS_PARTIAL_UPDATE
  // check WiFi and MQTT state
  uint8_t needsRedraw = 0;
  for (int i = 0; i < 20; i++)
  {
    // only check if WiFi was not connected already
    if (!stateWiFi)
    {
      if (WiFi.isConnected())
      {
        // schedule redraw for network and wifi
	bitSet(needsRedraw, REDRAW_NETWORK);
	bitSet(needsRedraw, REDRAW_WIFI);
	stateWiFi = true;
      }
    }

    // only check if MQTT was not connected already
    if (strlen(plateCfg.mqttHost) > 0 && !stateMQTT)
    {
      if (mqttConnected())
      {
        // schedule redraw for mqtt
	bitSet(needsRedraw, REDRAW_MQTT);
	stateMQTT = true;
      }
    }

    // finish loop early if both are connected
    if (stateWiFi && stateMQTT)
    {
      break;
    }

    // wait 100ms
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }

  // check for redraw
  if (needsRedraw)
  {
    // start partial update
    i2cStart();
    displayStart();
    display.selectDisplayMode(INKPLATE_1BIT);
    display.setTextColor(HP_FG, HP_BG);
    display.setTextSize(1);
    display.setFont(&FONT_BODY);
    display.partialUpdate(sleepBoot);

    if (bitRead(needsRedraw, REDRAW_NETWORK))
    {
      // redraw network
      Serial.println("[INFO] Partial update: Network");
      drawNetwork(&yNetwork, true);
    }
    if (bitRead(needsRedraw, REDRAW_WIFI))
    {
      // redraw wifi
      Serial.println("[INFO] Partial update: WiFi");
      drawWiFi(&yWiFi, true);
    }
    if (strlen(plateCfg.mqttHost) > 0 && bitRead(needsRedraw, REDRAW_MQTT))
    {
      // redraw mqtt
      Serial.println("[INFO] Partial update: MQTT");
      drawMQTT(&yMQTT, true);
    }

    // send to display
    display.partialUpdate(sleepBoot);
    displayEnd();
    i2cEnd();
  }
#endif // INKPLATE_HAS_PARTIAL_UPDATE
}
