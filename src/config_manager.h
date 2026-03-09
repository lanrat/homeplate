#pragma once

#include <cstdint>

struct HomePlateConfig {
    // Network
    char hostname[33];
    char staticIp[16];
    char staticSubnet[16];
    char staticGateway[16];
    char staticDns[16];

    // NTP & Time
    char ntpServer[65];
    char timezone[65]; // POSIX TZ string

    // Sleep
    uint16_t sleepMinutes;
    uint16_t quickSleepSec;

    // Content
    char imageUrl[257];
    char defaultActivityStr[20]; // "HomeAssistant", "Trmnl", etc.

    // TRMNL
    char trmnlUrl[257];
    char trmnlId[65];
    char trmnlToken[65];
    bool trmnlEnableLog;

    // Guest WiFi QR
    char qrWifiName[65];
    char qrWifiPassword[65];

    // MQTT
    char mqttHost[65];
    uint16_t mqttPort;
    char mqttUser[65];
    char mqttPassword[65];
    char mqttNodeId[33];
    char mqttDeviceName[33];
    uint32_t mqttExpireAfterSec; // 0 = auto-compute from sleep time

    // Display
    bool displayLastUpdateTime;

    // OTA
    bool enableOta;

    // Internal state (not user-editable)
    bool configured; // true if config has been saved to NVS at least once
};

extern HomePlateConfig plateCfg;

// Load config from NVS (with compile-time defaults as fallback)
void loadConfig();

// Save current config to NVS
void saveConfig();

// Check if config has been saved to NVS at least once
bool isConfigured();

// Log all config values to Serial (sensitive fields are masked)
void logConfig();

// Run WiFiManager for initial connection and config portal
// If forcePortal is true, always opens the config portal (even if WiFi connects)
// Returns true if WiFi connected, false on timeout
bool startWiFiManager(bool forcePortal = false);

// Display the config mode screen (AP SSID + QR code)
void displayConfigModeScreen(const char *apSsid);

// Display unconfigured/sleeping message
void displayUnconfiguredScreen();

// Get MQTT expire after sec (auto-computes if 0)
uint32_t getMqttExpireAfterSec();

// Get NTP sync interval (computed from sleep minutes)
uint16_t getNtpSyncInterval();
