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
  display.print("Hardware:");
  display.setCursor(COL1_DATA_X, y);
  display.print(CONFIG_IDF_TARGET);
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
  display.printf("%dMB %s", spi_flash_get_chip_size() / (1024 * 1024), (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
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
  // temp
  y += lineHeight;
  int temp = display.readTemperature();
  int tempF = (temp * 9 / 5) + 32;
  display.setCursor(COL1_NAME_X, y);
  display.print("Temperature:");
  display.setCursor(COL1_DATA_X, y);
  display.printf("%dC (%dF)", temp, tempF);

  // column 2
  y = 250;
  // network
  display.setCursor(COL2_NAME_X, y);
  display.print("Hostname:");
  display.setCursor(COL2_DATA_X, y);
  display.print(WiFi.getHostname());
  // ip
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.print("IP Address:");
  display.setCursor(COL2_DATA_X, y);
  display.print(WiFi.localIP().toString().c_str());
  // netmask
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.print("Subnet Mask:");
  display.setCursor(COL2_DATA_X, y);
  display.print(WiFi.subnetMask().toString().c_str());
  // gateway
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.print("Gateway:");
  display.setCursor(COL2_DATA_X, y);
  display.print(WiFi.gatewayIP().toString().c_str());
  // dns
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.print("DNS:");
  display.setCursor(COL2_DATA_X, y);
  display.print(WiFi.dnsIP(0).toString().c_str());
  // mac
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.print("MAC Address:");
  display.setCursor(COL2_DATA_X, y);
  display.print(WiFi.macAddress().c_str());

  // ssid
  y += lineHeight * 2;
  display.setCursor(COL2_NAME_X, y);
  display.print("SSID:");
  display.setCursor(COL2_DATA_X, y);
  display.print(WiFi.SSID().c_str());
  // bssid
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.print("BSSID:");
  display.setCursor(COL2_DATA_X, y);
  display.print(WiFi.BSSIDstr().c_str());
  // signal
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.print("Signal:");
  display.setCursor(COL2_DATA_X, y);
  display.printf("%d dBm", WiFi.RSSI());
  // wifi status
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.print("WiFi Status:");
  display.setCursor(COL2_DATA_X, y);
  display.print(wl_status_to_string(WiFi.status()));

  #ifdef MQTT_HOST
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
  #endif

  #ifdef NTP_SERVER
  // time
  y += lineHeight * 2;
  display.setCursor(COL2_NAME_X, y);
  display.print("Time:");
  display.setCursor(COL2_DATA_X, y);
  display.print(fullDateString());
  // NTP
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.print("NTP Server:");
  display.setCursor(COL2_DATA_X, y);
  display.print(NTP_SERVER);
  // NTP status
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.print("NTP Status:");
  display.setCursor(COL2_DATA_X, y);
  display.print(getNTPSynced() ? "OK" : "False");

  // RTC
  y += lineHeight;
  display.setCursor(COL2_NAME_X, y);
  display.printf("RTC:");
  display.setCursor(COL2_DATA_X, y);
  display.printf(display.rtcIsSet() ? "OK" : "Error");
  #endif

  displayBoundaryBox();
  display.display();
  displayEnd();
  i2cEnd();
}