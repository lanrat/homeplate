# TRMNL Setup

In order to use Homeplate with Trmnl you will either need [BYOD](https://docs.trmnl.com/go/diy/byod), [BYOS](https://docs.trmnl.com/go/diy/byos), or [both](https://docs.trmnl.com/go/diy/byod-s).

## Homeplate Config

Configure the following settings in the WiFi setup portal (see [setup.md](setup.md)):

| Setting | Value |
|---------|-------|
| Default Activity | `Trmnl` |
| TRMNL ID | Your Device ID from [trmnl.com/devices](https://trmnl.com/devices/) |
| TRMNL Token | Your API Key from the Device Credentials section |

The TRMNL URL defaults to `https://trmnl.app/api/display`. If you are running [BYOS](https://docs.trmnl.com/go/diy/byos), change it to your server URL.

You should also set the _Device Model_ to `Inkplate 10 - 1200x820` on the TRMNL website.
It is also a good idea to update the _MAC Address_ to your device's MAC Address as well.

## Sensors

Homeplate reports the device's internal temperature sensor to TRMNL via the `SENSORS` HTTP header. This is sent automatically with each display API request when a valid temperature reading is available. The temperature is read from the TPS65186 e-paper power management IC in degrees Celsius.

## Logging

Homeplate can optionally send device logs to TRMNL's `POST /api/log` endpoint. This is useful for remote debugging and monitoring device health. Logging is enabled by default when a TRMNL Token is configured. To disable, set `TRMNL Logging` to `false` in the WiFi setup portal.

When enabled, logs are batched and sent once per wake cycle at the end of each display update. Each log entry includes device status (battery voltage, WiFi signal, heap memory, wake reason, firmware version, etc.) along with event messages for boot, display updates, and errors.

## Home Assistant Config

The [Trmnl Alias Plugin](https://trmnl.com/integrations/alias) can be used to display a screenshot directly from your Home Assistant instance. You most likely want to set "Enable Cache" to `No` to ensure you always display a fresh image.

See the [Home Assistant](hass.md) documentation for more information on setting up Home Assistant screenshots.
