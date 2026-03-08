#pragma once

// HomePlate Configuration Defaults
//
// This file provides default values for all configurable settings.
// To override at compile time, create a config.h file (included before this).
// Most settings can also be changed at runtime via WiFiManager.

// ==== Runtime-configurable settings (with defaults) ====

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
#ifndef HOSTNAME
#define HOSTNAME "homeplate"
#endif
#ifndef STATIC_IP
#define STATIC_IP ""
#endif
#ifndef STATIC_SUBNET
#define STATIC_SUBNET ""
#endif
#ifndef STATIC_GATEWAY
#define STATIC_GATEWAY ""
#endif
#ifndef STATIC_DNS
#define STATIC_DNS ""
#endif
#ifndef NTP_SERVER
#define NTP_SERVER "pool.ntp.org"
#endif
#ifndef TIMEZONE
#define TIMEZONE "UTC0"
#endif
#ifndef TIME_TO_SLEEP_MIN
#define TIME_TO_SLEEP_MIN 20
#endif
#ifndef TIME_TO_QUICK_SLEEP_SEC
#define TIME_TO_QUICK_SLEEP_SEC (5 * 60)
#endif
#ifndef IMAGE_URL
#define IMAGE_URL ""
#endif
#ifndef DEFAULT_ACTIVITY_STR
#define DEFAULT_ACTIVITY_STR "HomeAssistant"
#endif
#ifndef TRMNL_URL
#define TRMNL_URL "https://trmnl.app/api/display"
#endif
#ifndef TRMNL_ID
#define TRMNL_ID ""
#endif
#ifndef TRMNL_TOKEN
#define TRMNL_TOKEN ""
#endif
#ifndef TRMNL_ENABLE_LOG_DEFAULT
#define TRMNL_ENABLE_LOG_DEFAULT true
#endif
#ifndef QR_WIFI_NAME
#define QR_WIFI_NAME ""
#endif
#ifndef QR_WIFI_PASSWORD
#define QR_WIFI_PASSWORD ""
#endif
#ifndef MQTT_HOST
#define MQTT_HOST ""
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif
#ifndef MQTT_USER
#define MQTT_USER ""
#endif
#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD ""
#endif
#ifndef MQTT_NODE_ID
#define MQTT_NODE_ID ""
#endif
#ifndef MQTT_DEVICE_NAME
#define MQTT_DEVICE_NAME "HomePlate"
#endif
#ifndef MQTT_EXPIRE_AFTER_SEC
#define MQTT_EXPIRE_AFTER_SEC 0
#endif
#ifndef DISPLAY_LAST_UPDATE_TIME
#define DISPLAY_LAST_UPDATE_TIME true
#endif
#ifndef ENABLE_OTA
#define ENABLE_OTA false
#endif

// ==== Compile-time only settings ====
// These cannot be changed at runtime via WiFiManager.
// To override, create a config.h file (see homeplate.h for include order).

// Disables touchpads if they are overly sensitive and result in phantom touch events.
// Must be false for boards without touchpads.
#ifndef TOUCHPAD_ENABLE
  #if defined(ARDUINO_INKPLATE10V2) || defined(ARDUINO_INKPLATE6V2) || defined(ARDUINO_INKPLATE6PLUS) || defined(ARDUINO_INKPLATE6PLUSV2)
    #define TOUCHPAD_ENABLE false
  #else
    #define TOUCHPAD_ENABLE true
  #endif
#endif

// For a custom sleep schedule, define CONFIG_CPP in config.h and implement
// sleepSchedule[] in config.cpp (see config_example.cpp).
// #define CONFIG_CPP
