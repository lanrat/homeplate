#include "homeplate.h"

#define COL1_NAME_X 1 * (E_INK_WIDTH / 8)
#define COL1_DATA_X 2 * (E_INK_WIDTH / 8)
#define COL2_NAME_X 5 * (E_INK_WIDTH / 8)
#define COL2_DATA_X 6 * (E_INK_WIDTH / 8)

const static int lineHeight = 20;

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
  display.fillRect(0, 0, 10, E_INK_HEIGHT, BLACK);                // left
  display.fillRect(E_INK_WIDTH - 10, 0, 10, E_INK_HEIGHT, BLACK); // right
  display.fillRect(0, 0, E_INK_WIDTH, 10, BLACK);                 // top
  display.fillRect(0, E_INK_HEIGHT - 10, E_INK_WIDTH, 10, BLACK); // bottom
}

void displayInfoScreen()
{
  Serial.printf("Rendering info page\n");
  static char buff[1024];
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);

  i2cStart();
  displayStart();
  display.selectDisplayMode(INKPLATE_1BIT);
  display.setTextColor(BLACK, WHITE);
  display.clearDisplay();

  // Title
  display.setFont(&Roboto_32);
  display.setTextSize(1);
  uint32_t y = centerTextX("HomePlate Info", 0, E_INK_WIDTH, 100, false);
  display.setFont(&Roboto_16);
  // version
  snprintf(buff, 1024, "Version: [%s]", VERSION);
  y = centerTextX(buff, 0, E_INK_WIDTH, y + 110, false);

  // column 1
  // HW
  y = 250;
  display.setCursor(COL1_NAME_X, y);
  display.printf("Hardware:");
  display.setCursor(COL1_DATA_X, y);
  display.printf(CONFIG_IDF_TARGET);
  // CPU
  y += lineHeight;
  display.setCursor(COL1_NAME_X, y);
  display.printf("CPU:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%u core(s)", chip_info.cores);
  // frequency
  y += lineHeight;
  display.setCursor(COL1_NAME_X, y);
  display.printf("Frequency:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%u Mhz", getCpuFrequencyMhz());
  // Features
  y += lineHeight;
  display.setCursor(COL1_NAME_X, y);
  display.printf("Features:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%s%s%s", (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi" : "", (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "", (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");
  // Flash
  y += lineHeight;
  display.setCursor(COL1_NAME_X, y);
  display.printf("Flash:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%dMB %s", spi_flash_get_chip_size() / (1024 * 1024), (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
  // Heap
  y += lineHeight;
  display.setCursor(COL1_NAME_X, y);
  display.printf("Min Heap Free:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%.2fMB", (esp_get_minimum_free_heap_size()) / (1024.0F * 1024.0F));
  // Tasks
  y += lineHeight;
  display.setCursor(COL1_NAME_X, y);
  display.printf("Tasks:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%d", uxTaskGetNumberOfTasks());
  // Display
  y += lineHeight;
  display.setCursor(COL1_NAME_X, y);
  display.printf("Display:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%dx%d", E_INK_WIDTH, E_INK_HEIGHT);

  // bootCount
  y += lineHeight * 2;
  display.setCursor(COL1_NAME_X, y);
  display.printf("Boot #:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%u", bootCount);
  // loopCount
  y += lineHeight;
  display.setCursor(COL1_NAME_X, y);
  display.printf("Activity #:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%u", activityCount);
  // wake reason
  y += lineHeight;
  display.setCursor(COL1_NAME_X, y);
  display.printf("Wake Reason:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%s", bootReason());
  // battery
  y += lineHeight;
  double voltage = display.readBattery();
  int percent = getBatteryPercent(voltage);
  display.setCursor(COL1_NAME_X, y);
  display.printf("Battery:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%d%% (%.2fv)", percent, voltage);
  // temp
  y += lineHeight;
  int temp = display.readTemperature();
  int tempF = (temp * 9 / 5) + 32;
  display.setCursor(COL1_NAME_X, y);
  display.printf("Temperature:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%dC (%dF)", temp, tempF);

  // column 2
  y = 250;
  // network
  display.setCursor(COL2_NAME_X, y);
  display.printf("Hostname:");
  display.setCursor(COL2_DATA_X, y);
  display.printf(WiFi.getHostname());
  // ip
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.printf("IP Address:");
  display.setCursor(COL2_DATA_X, y);
  display.printf(WiFi.localIP().toString().c_str());
  // netmask
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.printf("Subnet Mask:");
  display.setCursor(COL2_DATA_X, y);
  display.printf(WiFi.subnetMask().toString().c_str());
  // gateway
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.printf("Gateway:");
  display.setCursor(COL2_DATA_X, y);
  display.printf(WiFi.gatewayIP().toString().c_str());
  // dns
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.printf("DNS:");
  display.setCursor(COL2_DATA_X, y);
  display.printf(WiFi.dnsIP(0).toString().c_str());
  // mac
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.printf("MAC Address:");
  display.setCursor(COL2_DATA_X, y);
  display.printf(WiFi.macAddress().c_str());

  // ssid
  y += lineHeight * 2;
  display.setCursor(COL2_NAME_X, y);
  display.printf("SSID:");
  display.setCursor(COL2_DATA_X, y);
  display.printf(WiFi.SSID().c_str());
  // bssid
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.printf("BSSID:");
  display.setCursor(COL2_DATA_X, y);
  display.printf(WiFi.BSSIDstr().c_str());
  // signal
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.printf("Signal:");
  display.setCursor(COL2_DATA_X, y);
  display.printf("%d dBm", WiFi.RSSI());
  // wifi status
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.printf("WiFi Status:");
  display.setCursor(COL2_DATA_X, y);
  display.printf(wl_status_to_string(WiFi.status()));

  // MQTT server
  y += lineHeight * 2;
  display.setCursor(COL2_NAME_X, y);
  display.printf("MQTT Server:");
  display.setCursor(COL2_DATA_X, y);
  display.printf(MQTT_HOST);
  // MQTT status
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.printf("MQTT Status:");
  display.setCursor(COL2_DATA_X, y);
  display.printf(mqttConnected() ? "OK" : "Error");

  // time
  y += lineHeight * 2;
  display.setCursor(COL2_NAME_X, y);
  display.printf("Time:");
  display.setCursor(COL2_DATA_X, y);
  display.printf(fullDateString());
  // NTP
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.printf("NTP Server:");
  display.setCursor(COL2_DATA_X, y);
  display.printf(NTP_SERVER);
  // NTP status
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.printf("NTP Status:");
  display.setCursor(COL2_DATA_X, y);
  display.printf(getNTPSynced() ? "OK" : "False");
  // RTC
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.printf("RTC:");
  display.setCursor(COL2_DATA_X, y);
  display.printf(display.rtcIsSet() ? "OK" : "Error");

  displayBoundaryBox();
  display.display();
  displayEnd();
  i2cEnd();
}