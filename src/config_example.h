#pragma once
#ifndef CONFIG_H

// WiFi SSID
#define WIFI_SSID "WiFi Network Name" 
// WiFi password
#define WIFI_PASSWORD "WiFi Password"

// hostname
// NOTE: if using multiple homeplate devices, you MUST make the hostname unique
#define HOSTNAME "homeplate"

// How long to sleep between image refreshes
#define TIME_TO_SLEEP_MIN 20

// How long to sleep for quick activities like e.g. showing info and qr code (default 300 / 5 min)
//#define TIME_TO_QUICK_SLEEP_SEC 300

// How long before MQTT entries expire (default 2 * TIME_TO_SLEEP_MIN)
// When configuring a custom sleep schedule you might want to change this to a value greater than your longest sleep duration
//#define MQTT_EXPIRE_AFTER_SEC 3600

// Configure a custom sleep schedule
// NOTE: configure the actual sleep schedule in config.cpp, see config_example.cpp
//#define CONFIG_CPP
//#include "sleep_duration.h"
//extern SleepScheduleSlot sleepSchedule[];
//extern const size_t sleepScheduleSize;

// Static IP information
// If unset uses DHCP, but updates may be slower, set to use a Static IP
// #define STATIC_IP "192.168.1.10"
// #define STATIC_SUBNET "255.255.255.0"
// #define STATIC_GATEWAY "192.168.1.1"
// #define STATIC_DNS "192.168.1.1"

// NTP Time server to set RTC
#define NTP_SERVER "NTP Server IP"

// How often to re-sync the clock to NTP
#define NTP_SYNC_INTERVAL (24*60)/TIME_TO_SLEEP_MIN // ~ once a day when updating every TIME_TO_SLEEP_MIN minutes

// URL of PNG image to display
#define IMAGE_URL "HTTP URL of dashboard screenshot to display"

// Settings for Trmnl (optional)
// #define TRMNL_URL "https://trmnl.app/api/display"
// #define TRMNL_ID ""
// #define TRMNL_TOKEN ""
// #define DEFAULT_ACTIVITY Trmnl

// WiFi QR Code
#define QR_WIFI_NAME "Guest WiFi Network Name"
#define QR_WIFI_PASSWORD "Guest WiFi Password"

// MQTT Broker
#define MQTT_HOST "MQTT Broker IP"
#define MQTT_PORT 1883
// Set MQTT_USER & MQTT_PASSWORD if needed
//#define MQTT_USER "mqtt username"
//#define MQTT_PASSWORD "mqtt password"
// Customize node id and device name if needed
//#define MQTT_NODE_ID "homeplate"	// defaults to HOSTNAME
//#define MQTT_DEVICE_NAME "HomePlate"	// defaults to "HomePlate"

// Disables touchpads if they are overly sensitive and result in phantom touch events
// Touchpads are not supported on the Inkplate10v2. This must be false if ARDUINO_INKPLATE10V2 is set
#define TOUCHPAD_ENABLE true

// Timezone
// see timezone_config.h for options
#define TIMEZONE_UTC

// If your Inkplate doesn't have external (or second) MCP I/O expander, you should uncomment next line,
// otherwise your code could hang out when you send code to your Inkplate.
// You can easily check if your Inkplate has second MCP by turning it over and 
// if there is missing chip near place where "MCP23017-2" is written, but if there is
// chip soldered, you don't have to uncomment line and use external MCP I/O expander
//#define ONE_MCP_MODE

// Displays the time from the RTC whenever a new image is loaded
#define DISPLAY_LAST_UPDATE_TIME true

// keep this to signal the program has a valid config file
#define CONFIG_H
#endif
