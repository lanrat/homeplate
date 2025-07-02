# HomePlate Home Assistant Setup

## Screenshot Server

Install the [Puppet Home Assistant Addon](https://github.com/balloob/home-assistant-addons/tree/main/puppet) to take screenshots of your desired dashboards.

### Running via Docker Compose

If you are running Home Assistant in Docker, you can manually run the puppet addon by adding it to your `docker-compose.yaml`

First Clone the repository locally:

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
    extra_hosts:
      # this is optional, if homeassistant should resolve to a different IP, specify it here
      #- "homeassistant:host-gateway"
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

Replae `homeassistant` with the nostname or IP of your Home Assistant instance, and `DASHBOARD_URL` with the URL of the dashboard you want to screenshot.

The paramaters `viewport` and `eink` tell the addon to return a screenshot witht he Inkplate's native resolution and color depth.

See the [Puppet Home Assistant Addon](https://github.com/balloob/home-assistant-addons/tree/main/puppet) for more documentation and options.

Set this dashboard URL to `IMAGE_URL` in `config.h` or the Alias plugin URL if using [Trmnl](trmnl.md).

## MQTT

The HomePlate makes use of [MQTT Discovery](https://www.home-assistant.io/docs/mqtt/discovery/) so it should automatically add its sensors to your Home Assistant instance if MQTT is already setup.

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

This comes in handy with a commute automation for example, so in the morning, the display get's refreshed more often.
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
