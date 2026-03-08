# HomePlate Setup

## First-Time Setup

HomePlate uses a WiFi captive portal for configuration. No `config.h` file is required.

### 1. Flash the firmware

#### Option A: Web Installer (recommended)

Visit the [HomePlate Web Installer](https://lanrat.github.io/homeplate/) in Chrome or Edge on desktop. Select your board variant, connect your Inkplate via USB, and click **Install**. No development tools required. Pre-built firmware is also available on the [Releases](https://github.com/lanrat/homeplate/releases) page.

#### Option B: PlatformIO (for developers)

Install [PlatformIO](https://platformio.org/) and flash via USB. See the [Development Guide](developing.md) for full build and flash instructions.

### 2. Connect to the setup portal

On first boot (or when the device is unconfigured), HomePlate will:

1. Create a WiFi access point named **HomePlate-Setup**
2. Display the AP name and a QR code on the e-ink screen
3. Wait up to 15 minutes for configuration before sleeping

Connect to the **HomePlate-Setup** WiFi network from your phone or computer. A captive portal page will open automatically (or navigate to `192.168.4.1`).

### 3. Configure WiFi and settings

The portal has two pages:

- **WiFi Configuration** — Select your WiFi network and enter the password
- **Setup** — Configure all HomePlate settings (see [Settings Reference](#settings-reference) below)

After saving, the device will reboot and connect to your WiFi network with the new settings.

## Changing Settings

### Wake button method

Hold the **wake button** while the device boots (press reset or wait for a sleep wake cycle) to force the config portal to open. The device will connect to your saved WiFi and open the **HomePlate-Setup** AP simultaneously, allowing you to reconfigure any settings.

### Re-flash method

You can also re-flash the firmware to trigger setup mode again — the config portal will open if the device detects its activity is missing required settings.

### Resetting all settings

To clear all saved settings and start fresh, use the [Web Installer](https://lanrat.github.io/homeplate/) and select the option to erase the device during installation. For PlatformIO users, see the [Development Guide](developing.md#resetting-all-settings).

## Settings Reference

All settings below can be configured through the WiFi portal. They are saved to the device's non-volatile storage (NVS) and persist across reboots and firmware updates.

### Network

| Setting        | Description                               | Default     |
|----------------|-------------------------------------------|-------------|
| Hostname       | Device hostname (mDNS, OTA, MQTT topics)  | `homeplate` |
| Static IP      | Static IP address (blank = DHCP)          | blank       |
| Static Subnet  | Subnet mask for static IP                 | blank       |
| Static Gateway | Gateway for static IP                     | blank       |
| Static DNS     | DNS server for static IP                  | blank       |

### Time

| Setting    | Description                                                   | Default        |
|------------|---------------------------------------------------------------|----------------|
| NTP Server | NTP time server                                               | `pool.ntp.org` |
| Timezone   | POSIX TZ string (see [Timezone Strings](#timezone-strings))   | `UTC0`         |

### Sleep

| Setting             | Description                           | Default       |
|---------------------|---------------------------------------|---------------|
| Sleep Minutes       | Minutes between display refreshes     | `20`          |
| Quick Sleep Seconds | Sleep duration for Info/QR activities | `300` (5 min) |

### Content

| Setting          | Description                                                            | Default         |
|------------------|------------------------------------------------------------------------|-----------------|
| Image URL        | URL of PNG image to display (for HomeAssistant activity)               | blank           |
| Default Activity | Activity to run on boot: `HomeAssistant`, `Trmnl`, `Info`, `GuestWifi` | `HomeAssistant` |

### TRMNL

| Setting       | Description                                                    | Default                         |
|---------------|----------------------------------------------------------------|---------------------------------|
| TRMNL URL     | TRMNL API endpoint                                             | `https://trmnl.app/api/display` |
| TRMNL ID      | Device ID from [trmnl.com/devices](https://trmnl.com/devices/) | blank                           |
| TRMNL Token   | API Key from TRMNL Device Credentials                          | blank                           |
| TRMNL Logging | Send device logs to TRMNL (`true`/`false`)                     | `true`                          |

### Guest WiFi QR Code

| Setting          | Description                   | Default |
|------------------|-------------------------------|---------|
| QR WiFi SSID     | WiFi network name for QR code | blank   |
| QR WiFi Password | WiFi password for QR code     | blank   |

### MQTT

| Setting               | Description                                | Default     |
|-----------------------|--------------------------------------------|-------------|
| MQTT Host             | MQTT broker hostname (blank = disabled)    | blank       |
| MQTT Port             | MQTT broker port                           | `1883`      |
| MQTT User             | MQTT username                              | blank       |
| MQTT Password         | MQTT password                              | blank       |
| MQTT Node ID          | Node ID for MQTT topics (blank = hostname) | blank       |
| MQTT Device Name      | Device name in Home Assistant              | `HomePlate` |
| MQTT Expire After Sec | Sensor expiry time in seconds (0 = auto)   | `0`         |

### Display & OTA

| Setting          | Description                                           | Default |
|------------------|-------------------------------------------------------|---------|
| Show Update Time | Display timestamp on image updates (`true`/`false`)   | `true`  |
| Enable OTA       | Enable over-the-air firmware updates (`true`/`false`) | `false` |

## Timezone Strings

The timezone setting uses POSIX TZ strings. Common examples:

| Timezone          | POSIX String                   |
|-------------------|--------------------------------|
| US Pacific        | `PST8PDT,M3.2.0,M11.1.0`       |
| US Mountain       | `MST7MDT,M3.2.0,M11.1.0`       |
| US Central        | `CST6CDT,M3.2.0,M11.1.0`       |
| US Eastern        | `EST5EDT,M3.2.0,M11.1.0`       |
| UTC               | `UTC0`                         |
| UK / Ireland      | `GMT0BST,M3.5.0/1,M10.5.0`     |
| Central Europe    | `CET-1CEST,M3.5.0,M10.5.0/3`   |
| Australia Eastern | `AEST-10AEDT,M10.1.0,M4.1.0/3` |
| Japan             | `JST-9`                        |
| India             | `IST-5:30`                     |

For a full list, see [POSIX TZ database](https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv).

## Advanced: Compile-Time Configuration

For advanced users who build from source, compile-time defaults and additional settings are available. See the [Development Guide](developing.md#advanced-compile-time-configuration) for details.
