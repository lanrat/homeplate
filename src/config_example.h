#pragma once

// WiFi SSID
#define WIFI_SSID "WiFi Network Name" 
// WiFi password
#define WIFI_PASSWORD "WiFi Password"

// hostname
// NOTE: if using multiple homeplate devices, you MUST make the hostname unique
#define HOSTNAME "homeplate"

// Static IP information
// If unset uses DHCP, but updates may be slower, set to use a Static IP
// #define STATIC_IP "192.168.1.10"
// #define STATIC_SUBNET "255.255.255.0"
// #define STATIC_GATEWAY "192.168.1.1"

// NTP Time server to set RTC
#define NTP_SERVER "NTP Server IP"

// URL of PNG image to display
#define IMAGE_URL "HTTP URL of dashboard screenshot to display"

// WiFi QR Code
#define QR_WIFI_NAME "Guest WiFi Network Name"
#define QR_WIFI_PASSWORD "Guest WiFi Password"

// MQTT Broker
#define MQTT_HOST "MQTT Broker IP"
#define MQTT_PORT 1883
// Set MQTT_USER & MQTT_PASSWORD if needed
//#define MQTT_USER "mqtt username"
//#define MQTT_PASSWORD "mqtt password"

// Disables touchpads if they are overly sensitive and result in phantom touch events
#define TOUCHPAD_ENABLE true

// If your Inkplate doesn't have external (or second) MCP I/O expander, you should uncomment next line,
// otherwise your code could hang out when you send code to your Inkplate.
// You can easily check if your Inkplate has second MCP by turning it over and 
// if there is missing chip near place where "MCP23017-2" is written, but if there is
// chip soldered, you don't have to uncomment line and use external MCP I/O expander
//#define ONE_MCP_MODE

// keep this to signal the program has a valid config file
#define CONFIG_H