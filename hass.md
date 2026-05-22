# HomePlate Home Assistant Setup

## Screenshot Server

Install the [Puppet Home Assistant Addon](https://github.com/balloob/home-assistant-addons/tree/main/puppet) to take screenshots of your desired dashboards.

### Running via Docker Compose

If you are running Home Assistant in Docker, you can manually run the puppet addon by adding it to your `docker-compose.yaml`

First clone the repository:

```shell
git clone https://github.com/balloob/home-assistant-addons balloob_addons
```

Update `docker-compose.yaml`:

```yaml
  hass-screenshot:
    container_name: hass-screenshot
    image: balloob_ha_puppet
    build: balloob_addons/puppet
    pull_policy: never
    deploy:
      resources:
        limits:
          memory: 1G
    restart: unless-stopped
    volumes:
      - ./puppet_config.json:/data/options.json:ro
    depends_on:
      - homeassistant
    # this section is optional, if homeassistant should resolve to a different IP, specify it here
    # extra_hosts:
    #  - "homeassistant:host-gateway"
    ports:
      - 10000:10000
```

Create `./puppet_config.json`. Be sure to set `access_token` for your instance.

```json
{
  "access_token": "****",
  "keep_browser_open": false
}
```

Bring it up:

```shell
docker compose build
docker compose up -d
```

### Get Screenshots of dashboards

Visit `http://homeassistant:10000/DASHBOARD_URL?viewport=1200x825&eink=8` to verify everything is working.

Replace `homeassistant` with the hostname or IP of your Home Assistant instance, and `DASHBOARD_URL` with the URL of the dashboard you want to screenshot.

The parameters `viewport` and `eink` tell the addon to return a screenshot with the Inkplate's native resolution and color depth.

See the [Puppet Home Assistant Addon](https://github.com/balloob/home-assistant-addons/tree/main/puppet) for more documentation and options.

Set this dashboard URL as the `Image URL` in the WiFi setup portal (see [setup.md](setup.md)), or use the Alias plugin URL if using [Trmnl](trmnl.md).

## MQTT

The HomePlate makes use of [MQTT Discovery](https://www.home-assistant.io/docs/mqtt/discovery/) so it should automatically add its sensors to your Home Assistant instance if MQTT is already setup.

> **Note:** Temperature is only reported on hardware equipped with a temperature sensor.

For example dashboard yaml see [dashboard.md](dashboard.md).

### MQTT Commands

You can change the activity running on the HomePlate by publishing the following MQTT message to the topic: `homeplate/<mqtt_node_id>/activity/run` which defaults to `homeplate/homeplate/activity/run`

The example below launches the QR activity:

```json
{
    "action": "qr"
}
```

You can add additional data to the action as well. To display a text message:

```json
{
    "action": "message",
    "message": "Hello World!"
}
```

If you want to override the sleep time for the next action, for example to just display the QR code for 1 minute:

```json
{
    "action": "qr",
    "refresh": "60"
}
```

This comes in handy with a commute automation for example, so in the morning, the display gets refreshed more often.
The following automation runs every 5 minutes between 7:30 and 10:30, updates the commute sensor and sends an activity trigger
with a 5 minute refresh timer for the next boot:

```yaml
- alias: "commute example"
  trigger:
    - platform: time_pattern
      minutes: "/5"
  condition:
    - condition: time
      after: "07:30:00"
      before: "10:30:00"
    - condition: time
      weekday:
        - mon
        - tue
        - wed
        - thu
        - fri
  action:
    - service: homeassistant.update_entity
      target:
        entity_id: sensor.commute_example
    - service: mqtt.publish
      data:
        topic: homeplate/homeplate/activity/run
        qos: '1'
        payload: '{ "action": "hass", "refresh": "300" }'
        retain: true
```

### Display an Image with Dither Override

The `img` action downloads and displays an image from a URL:

```json
{
    "action": "img",
    "message": "https://example.com/photo.jpg"
}
```

The dither kernel used to convert the image to e-ink can be overridden per-request via the optional `"dither"` field. When unset, the device falls back to the default kernel configured in the WiFiManager portal.

```json
{
    "action": "img",
    "message": "https://example.com/photo.jpg",
    "dither": "atkinson"
}
```

```json
{
    "action": "img",
    "message": "https://example.com/screenshot.png",
    "dither": "none"
}
```

Accepted names are case-insensitive and ignore spaces, hyphens, and underscores: `none` / `false` / `off` / `""`, `floyd-steinberg`, `jarvis-judice-ninke`, `atkinson`, `burkes`, `stucki`, `sierra-lite`, `reduced-diffusion`. Unknown names log an error and fall back to the configured default.

#### HTTP `X-Dither` response header

For HTTP image requests, the image server can also set the dither by returning an `X-Dither: <name>` response header. The same name set is accepted. When both an MQTT `"dither"` field and the HTTP header are present, **the HTTP header wins** (the server hosting the image is closer to knowing what dither suits it).

#### Discovering supported names from MQTT

On every MQTT connect, HomePlate publishes the full list of accepted dither names as a retained JSON array to:

```text
homeplate/<mqtt_node_id>/dither/options
```

Example payload:

```json
["none","Floyd-Steinberg","Jarvis-Judice-Ninke","Atkinson","Burkes","Stucki","Sierra-Lite","Reduced-Diffusion"]
```

Names are matched case-insensitively and ignore hyphens/underscores/spaces, so `"atkinson"`, `"Atkinson"`, and `"AT KIN SON"` all work.

Use this in automations or HA templates instead of hardcoding the list.

### Configuration Entities

Settings that previously could only be changed through the WiFiManager setup portal are now exposed as native Home Assistant entities via MQTT Discovery. They appear under the existing HomePlate device in HA, in the "Configuration" section. Changes are persisted to NVS and survive reboots.

| Entity | HA component | Key (topic slug) | Range / options | Apply timing |
|---|---|---|---|---|
| Sleep Minutes | number | `sleep_min` | 1–1440 min | next sleep |
| Quick Sleep Seconds | number | `quick_sleep` | 0–86400 s | next quick sleep |
| Default Activity | select | `def_activity` | HomeAssistant / Trmnl / Info / GuestWifi | next default dispatch |
| Image URL | text | `image_url` | up to 256 chars | next HomeAssistant activity |
| TRMNL URL | text | `trmnl_url` | up to 256 chars | next TRMNL activity |
| TRMNL ID | text | `trmnl_id` | up to 64 chars | next TRMNL activity |
| TRMNL Token | text (password) | `trmnl_token` | up to 64 chars | next TRMNL activity |
| TRMNL Logging | switch | `trmnl_log` | ON / OFF | next TRMNL activity |
| Dither Kernel | select | `dither_kern` | None + each kernel name | next image render |
| Show Update Time | switch | `disp_time` | ON / OFF | next render |
| Timezone | text | `timezone` | POSIX TZ string | immediate (no reboot) |
| Guest WiFi SSID | text | `qr_name` | up to 64 chars | next GuestWifi render |
| Guest WiFi Password | text (password) | `qr_pass` | up to 64 chars | next GuestWifi render |
| Reboot | button | `cmd/reboot` | — | immediate |
| Enter Setup Mode | button | `cmd/setup_mode` | — | reboots into WiFiManager portal |

**Topics** (where `<node>` is `mqtt_node_id`, default `homeplate`):

- State (retained): `homeplate/<node>/config/<key>/state` — raw scalar (e.g. `30`, `Atkinson`, `ON`).
- Command: `homeplate/<node>/config/<key>/set` — same scalar form HA publishes.
- Button command: `homeplate/<node>/cmd/reboot`, `homeplate/<node>/cmd/setup_mode` — payload ignored (HA sends `PRESS`).

**Excluded by design** (require reboot or are network-critical, so they stay portal-only): `hostname`, static IP fields, MQTT broker host/port/credentials, NTP server, OTA enable.

**Sensitive fields:** `trmnl_token` and `qr_pass` are exposed as HA password-mode text. The values still pass over MQTT in cleartext and are retained on the broker — anyone with broker access can read them. If that is unacceptable, leave them blank in HA and set them only via the WiFiManager portal.

**Idempotency:** every command is value-compared against the current setting before NVS is rewritten, so retained replays on reconnect do not cause repeated flash writes.

### Home Plate Card Example

![Home Assistant card](https://user-images.githubusercontent.com/164192/151242986-a8ed6948-3462-4d02-80f4-9a08062d237b.png)

```yaml
type: vertical-stack
cards:
  - type: entities
    entities:
      - entity: sensor.homeplate_boot_count
        secondary_info: last-updated
      - entity: sensor.homeplate_activity_count
        secondary_info: last-updated
      - entity: sensor.homeplate_temperature
        secondary_info: last-updated
      - entity: sensor.homeplate_voltage
        secondary_info: last-updated
      - entity: sensor.homeplate_battery
        secondary_info: last-updated
      - entity: sensor.homeplate_boot_reason
        secondary_info: last-updated
      - entity: sensor.homeplate_wifi_signal
        secondary_info: last-updated
      - entity: sensor.homeplate_sleep_duration
        secondary_info: last-updated
    title: HomePlate
    state_color: false
  - type: grid
    cards:
      - type: button
        tap_action:
          action: call-service
          service: mqtt.publish
          service_data:
            topic: homeplate/homeplate/activity/run
            qos: '1'
            payload: '{ "action": "hass" }'
            retain: true
          target: {}
        icon: mdi:home-assistant
        name: Dashboard
      - type: button
        tap_action:
          action: call-service
          service: mqtt.publish
          service_data:
            topic: homeplate/homeplate/activity/run
            qos: '1'
            payload: '{ "action": "qr" }'
            retain: true
          target: {}
        icon: mdi:qrcode
        name: WiFi
      - type: button
        tap_action:
          action: call-service
          service: mqtt.publish
          service_data:
            topic: homeplate/homeplate/activity/run
            qos: '1'
            payload: '{ "action": "info" }'
            retain: true
          target: {}
        icon: mdi:information-outline
        name: Info
      - type: button
        tap_action:
          action: call-service
          service: mqtt.publish
          service_data:
            topic: homeplate/homeplate/activity/run
            qos: '1'
            retain: true
            payload_template: >-
              { "action": "message",  "message": "{{
              states('input_text.homeplate_message') }}"}
          target: {}
        icon: mdi:message
        name: Message
      - type: button
        tap_action:
          action: call-service
          service: mqtt.publish
          service_data:
            topic: homeplate/homeplate/activity/run
            qos: '1'
            retain: true
            payload_template: >-
              { "action": "img",  "message": "{{ states('input_text.homeplate_message')
              }}"}
          target: {}
        icon: mdi:image
        name: Image
    columns: 4
  - type: entities
    entities:
      - entity: input_text.homeplate_message
        name: Message
    show_header_toggle: false
```
