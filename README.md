# HomePlate

A [Trmnl](https://trmnl.com/) and [Home Assistant](https://www.home-assistant.io/) E-Ink Dashboard on the [Inkplate 10](https://soldered.com/product/inkplate-10-9-7-e-paper-board-copy/)

![Home Assistant](screenshots/hass.jpeg)

## [Activities Screenshots](activities.md)

## Features

* [Trmnl](https://trmnl.com) support
* Display Home Assistant dashboards on a beautiful e-ink display
* Display WiFi QR Codes for guests/friends to connect to home/guest wifi
* Display messages directly from Home Assistant over MQTT
* Makes full use of the ESP32's cores with [FreeRTOS](https://www.freertos.org/)
* Reports sensor data to Home Assistant over MQTT (Temperature, Battery, WiFi, etc.)
* Change activity displayed via MQTT command (HASS dashboard, WiFi QR, Stats, text message, etc.)
* Syncs RTC over NTP
* Touch-pad buttons can start activities and wake from sleep
* 1 month+ battery life!
* Low battery warning displayed and sent over MQTT
* OTA updates over WiFi
* Partial screen updates in grayscale mode.
* Power saving sleep mode.
* Display any image from MQTT command
* Supports PNG, BMP, and JPEG images

## Setup

### [Quick Start Guide](setup.md)

HomePlate is configured through a WiFi captive portal — no `config.h` file is required. Flash the firmware, connect to the **HomePlate-Setup** WiFi network, and configure your settings through the web interface.

See [setup.md](setup.md) for detailed setup instructions, settings reference, and timezone configuration.

To change settings later, hold the **wake button** during boot to re-open the config portal.

### [Hardware](hardware.md)

### [Trmnl](trmnl.md)

Set the `TRMNL ID` and `TRMNL Token` in the WiFi setup portal, and set the Default Activity to `Trmnl`.

The [Alias Plugin](https://help.trmnl.com/en/articles/10701448-alias-plugin) can be used to display images from your local network, such as a Home Assistant Dashboard.

See [trmnl.md](trmnl.md) for more information.

### Home Assistant Dashboard

Create a Home Assistant Dashboard you want to display. I recommend using the [kiosk-mode](https://github.com/NemesisRE/kiosk-mode), [card-mod](https://github.com/thomasloven/lovelace-card-mod) and [layout-card](https://github.com/thomasloven/lovelace-layout-card) plugins to customize and tune the dashboard for your display.

Setup the [Screenshot Home Assistant using Puppeteer](https://github.com/balloob/home-assistant-addons/tree/main/puppet) service to create screenshots of the desired dashboards for the HomePlate. This also works with the Trmnl Alias plugin.

### More information

See [hass.md](hass.md) and [dashboard.md](dashboard.md) for additional details.

### Building

Install [PlatformIO](https://platformio.org/).

```shell
pio run -e inkplate10    # Inkplate 10 (original with touchpads)
pio run -e inkplate10v2  # Inkplate 10v2 (without touchpads)
```

The first flash/installation needs to be done over USB. Future updates can be done over USB or WiFi with:

```shell
pio run -e ota
```

To monitor serial output without re-flashing:

```shell
pio device monitor
```

### Updating

```shell
git pull
pio upgrade
pio pkg update
pio run --target clean
```

### Debugging

#### Touchpad Sensitivity

On some devices, the touchpads can be overly sensitive. This can cause lots of phantom touch events preventing the Homeplate from going into sleep and using up a lot of power.

Sometimes running `pio run --target=clean` can resolve this before you build & flash the firmware.

The touchpad sensitivity is set in hardware by resistors, but the touch sensors are calibrated on bootup when the Device first gets power. I have found that USB power can mess with this calibration. If you are using battery power, restarting the Homeplate (by using the power switch on the side of the PCB) without USB power attached is enough to fix the sensitivity.

Alternatively, the touchpads can be completely disabled by adding `#define TOUCHPAD_ENABLE false` in a `src/config.h` file (this is a compile-time only setting and cannot be changed via the WiFi portal). Touchpads are automatically disabled when building for the Inkplate 10v2.

#### Waveform

If you get the following error while booting your inkplate, run the [Inkplate_Wavefrom_EEPROM_Programming](https://github.com/SolderedElectronics/Inkplate-Arduino-library/tree/master/examples/Inkplate10/Diagnostics/Inkplate10_Wavefrom_EEPROM_Programming) example to update your Inkplate's waveform.

```text
Waveform load failed! Upload new waveform in EEPROM. Using default waveform.
```

Older Inkplates don't appear to ship with an updated waveform. I found waveform 3 looks the best for mine.

### Tests

The available unit tests use the 'native' environment and can be run by either:

* running them manually

```shell
pio test -v
```

* in VSCode use Testing -> native -> Run Test
* in VSCode use PlatformIO -> Project Tasks -> native -> Advanced -> Test
