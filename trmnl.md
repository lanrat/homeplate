# TRMNL Setup

## Homeplate Config

Add the following to your `config.h`

```c
// Settings for Trmnl
#define TRMNL_URL "https://trmnl.app/api/display"
#define TRMNL_ID ""
#define TRMNL_TOKEN ""
#define DEFAULT_ACTIVITY Trmnl
```

If you are running [BYOS](https://docs.usetrmnl.com/go/diy/byos) change `TRMNL_URL` to be your server URL.

You can obtain `TRMNL_ID` and `TRMNL_TOKEN` by visting [usetrmnl.com/devices](https://usetrmnl.com/devices/).
`TRMNL_ID` is the _Device ID_.
`TRMNL_TOKEN` is the _API Key_ in the _Device Credentials_ section.
You should also set the _Device Model_ to `Inkplate 10 - 1200x820`.
It is also a good diea to update the __MAC Address_ to your device's MAC Address as well.

## Home Assistant Config

The [Trmnl Alias Plugin](https://usetrmnl.com/integrations/alias) can be used to display a screenshot directly from your Home Assistant instance. You most likely want to set "Enable Cache" to `No` to ensure you always display a fresh image.

See the [Home Assistant](hass.md) documentation for more information on setting up Home Assistant screenshots.
