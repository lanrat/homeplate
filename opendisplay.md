# OpenDisplay Setup

[OpenDisplay](https://opendisplay.org) is an open standard for delivering images to e-ink displays from a central server (Home Assistant, a Python script, etc.) over Wi-Fi or BLE. HomePlate implements the **Flex profile over Wi-Fi LAN** so HomePlate panels can be driven by any OpenDisplay-compatible server.

In OpenDisplay terminology HomePlate is the **device**: it advertises itself on the LAN via mDNS and accepts an incoming TCP connection from the **server** (the controller that owns the source image, dithers it, and pushes it to the device).

## HomePlate Config

Configure these in the WiFi setup portal (see [setup.md](setup.md)) or via the Home Assistant MQTT config entities (see [hass.md](hass.md#configuration-entities)):

| Setting | Value |
| ------- | ----- |
| Default Activity | `OpenDisplay` |
| OpenDisplay Port | TCP port to listen on. Default `2446` (matches OpenDisplay's manufacturer ID). |
| OpenDisplay Listen Seconds | How long after each wake the device waits for a controller to connect. Default `60`. |

There is no token / secret to configure — the LAN connection is unauthenticated in this release (see Limitations below).

## How it works

On every wake cycle the device:

1. Starts a TCP listener on the configured port and advertises `_opendisplay._tcp.local` via mDNS so the server can discover it. The mDNS record includes a `msd` TXT entry with the device's battery voltage, temperature, and reboot/loop-counter status — controllers can read these without opening a TCP connection. On boards without a temperature sensor (Inkplate 6 COLOR), the temperature byte is left at the encoded floor (-40°C) as a "no real reading" sentinel — the OpenDisplay wire format has no dedicated missing-temperature code.
2. Displays `Waiting for controller (60s)` on the panel.
3. Blocks until either a controller connects or the listen window expires.
4. If a controller connects, runs the OpenDisplay direct-write upload (`0x70` → N×`0x71` → `0x72`), renders the received image, and signals refresh complete (`0x73`).
5. Tears down and lets HomePlate's normal sleep path take over.

The wake/sleep schedule (how often the device wakes up to listen for a new image) is governed by the standard `Sleep Minutes` setting, the same as every other HomePlate activity.

## Server Setup

Any OpenDisplay-compatible server should work. Two options to start with:

- **Home Assistant** — install the OpenDisplay integration from HACS. Once the integration is installed, HomePlate should appear automatically when it's awake and advertising. See the OpenDisplay integration docs for dashboard / image-source setup.
- **[py-opendisplay](https://github.com/OpenDisplay/py-opendisplay)** — a Python library for driving OpenDisplay devices, useful for testing. A minimal upload script connects to `homeplate.local:2446`, sends the direct-write sequence with a pre-dithered monochrome bitplane, and disconnects.

Because dithering happens on the server, the server must know the panel's dimensions and color scheme. HomePlate currently reports as **monochrome at the panel's native resolution** — see Limitations.

## Known Limitations

This is an MVP. Several things are intentionally out of scope for the first release; they will be revisited based on usage.

- **Wi-Fi only.** No BLE transport. (BLE is supported by the OpenDisplay protocol but adds significant flash / power cost on the classic ESP32 the Inkplate uses.)
- **No encryption / authentication.** OpenDisplay's optional AES-128 challenge-response (`0x0050`) is not implemented. Anyone on the same LAN can push images to your HomePlate. Treat this like an unauthenticated TRMNL: fine on a trusted home network, not safe to expose externally.
- **Read-only configuration over OpenDisplay.** The device responds to `READ_CONFIG` (`0x0040`) and `READ_FW_VERSION` (`0x0043`) with bare ACKs (no payload). Controllers that strictly require a TLV configuration blob to learn panel dimensions before sending may not work — open an issue with the server output if you hit this. Use HomePlate's existing setup portal / MQTT entities for configuration instead.
- **Monochrome bitplane rendering only.**
  - **3-bit grayscale Inkplates** (all the B&W boards: 5, 6, 6v2, 6 Plus, 6 Plus v2, 6 Flick, 10, 10v2) report their color scheme as `monochrome`. The OpenDisplay Flex color schemes don't include grayscale, so the server will dither to 1-bit before sending. Grayscale detail visible in the Home Assistant or TRMNL paths is lost when driven via OpenDisplay.
  - **Inkplate 6 COLOR** (7-color ACeP panel) also reports monochrome in this release. The 6-color OpenDisplay scheme could in theory map to this panel but the palette mapping is not yet implemented; you'll see a monochrome image on a color panel. Tracked as a follow-up.
- **Full refresh only.** No partial-update support (`0x0076`). Every image is a full panel refresh.
- **One image per wake cycle.** The device only listens for `Listen Seconds` after wake; after that it sleeps until the next scheduled wake. Server-pushed updates while the device is asleep do not work — by design, this is what makes long battery life possible.
- **No LED commands** (`0x0073` flash) — not applicable to Inkplate panels.

## Testing locally

A small Python test script lives in [opendisplay-test/](opendisplay-test/) — it pushes a single image to a HomePlate without needing Home Assistant or any other controller. Useful for verifying the firmware end-to-end and as a known-good reference for the wire protocol. See [opendisplay-test/README.md](opendisplay-test/README.md) for usage.

```sh
cd opendisplay-test
./opendisplay_test.py inkplate10.png
```

Watch the device serial output (`pio device monitor -e <board>`) for the `[OD]` log lines: you should see the START, periodic DATA frames being acknowledged, the END, the decompression size, and the panel refresh.
