# Home Assistant Dashboard Examples

This page includes some example yaml to help style your lovelace cards.

All cards use [card-mod](https://github.com/thomasloven/lovelace-card-mod) to override the CSS of each card.

## View

For the view layout itself, I use
[lovelace-layout-card](https://github.com/thomasloven/lovelace-layout-card) with the "Vertical (layout-card)" View type with the following config:

```yaml
max_width: 400
max_cols: 3
```

## Weather

<img width="414" src="https://user-images.githubusercontent.com/164192/168654417-8a2723a7-ed51-4bc7-8197-a26a510f1674.png">

```yaml
type: weather-forecast
entity: weather.weather
name: ' '
secondary_info_attribute: humidity
show_forecast: true
card_mod:
  style: |
    ha-card {
      background: none;
      box-shadow: none;
      padding: 0 !important;
    }
    ha-card > div > div > div > div.attribute {
      color: #2f2f2f !important;
    }
    ha-card > div > div > div.templow {
      color: #6f6f6f !important;
    }
    .cloud-back {
      fill: #8f8f8f !important;
    }
    .cloud-front {
      fill: #cfcfcf !important;
    }
    .rain {
      fill: #2f2f2f !important;
    }
    .sun {
      fill: #cfcfcf !important;
    }
    .moon {
      fill: #6f6f6f !important;
    }
```

## Sun

<img width="405" src="https://user-images.githubusercontent.com/164192/168654644-e97b6981-9329-4d88-b494-2e0fddd834ee.png">

```yaml
type: custom:sun-card
card_mod:
  style: |
    ha-card {
      background: none;
      box-shadow: none;
      filter: grayscale(1);
      padding: 0 !important;

    }
    .sun-card-text-subtitle {
      color: #2f2f2f !important;
    }
    ha-card > div > div.sun-card-body > svg > path:nth-child(3) {
      /* morning shade */
      filter: brightness(80%) contrast(90%);
      opacity: 0.5;
    }
    ha-card > div > div.sun-card-body > svg > path:nth-child(5) {
      /* evening shade */
      filter: brightness(80%) contrast(90%);
      opacity: 0.5;
    }
    ha-card > div > div.sun-card-body > svg > path:nth-child(4) {
      /* daytime shade */
      filter: brightness(70%) contrast(150%);
      opacity: 0.5;
    }
    path.sun-card-sun-line {
      /* sun path line */
      opacity: 0;
    }
    ha-card > div > div.sun-card-body > svg > circle {
      /* sun */
      /*filter: brightness(130%) contrast(100%); */
    }
    line:nth-child(6) {
      /* horizon line */
      opacity: 0;
    }
    line:nth-child(7) {
      /* morning vertical line */
      opacity: 0;
    }
    line:nth-child(8) {
      /* evening vertical line */
      opacity: 0;
    }
```

## Mini Graph Card

<img width="204" src="https://user-images.githubusercontent.com/164192/168654937-f59b4a51-618e-4446-bbd1-b80affd23488.png">

```yaml
type: custom:mini-graph-card
name: Outside
decimals: 1
align_state: center
line_width: 6
points_per_hour: 2
show:
  legend: false
  icon: false
entities:
  - entity: sensor.outside_temperature
    show_state: true
    color: '#2f2f2f'
  - entity: sensor.outside_humidity
    show_state: true
    y_axis: secondary
    color: '#8f8f8f'
card_mod:
  style: |
    ha-card {
      background: none;
      box-shadow: none;      
    }
    ha-card > div > div > span {
      font-weight: 700 !important;  
    }
    ha-card > div {
      font-weight: 700 !important;  
      font-size: 8pt !important; 
    }
    ha-card > div > div.name > span.ellipsis {
      font-size: 16pt !important; 
    }
```

## Apex Charts Card

<img width="392" src="https://user-images.githubusercontent.com/164192/168655215-4e2b0ff2-d7f6-435f-9a6d-9e6d8cd7d8dc.png">

```yaml
type: custom:apexcharts-card
update_interval: 5min
show:
  loading: false
apex_config:
  xaxis:
    tooltip:
      enabled: false
  grid:
    borderColor: grey
  legend:
    show: false
  chart:
    height: 150px
  yaxis:
    forceNiceScale: true
    decimalsInFloat: 0
header:
  show: true
  show_states: true
  colorize_states: true
  title: Particulate Matter
all_series_config:
  show:
    legend_value: false
  stroke_width: 3
series:
  - entity: sensor.particulate_matter_2_5um_concentration
    name: Inside
    color: '#6f6f6f'
    show:
      in_header: true
    group_by:
      func: avg
      duration: 15min
  - entity: sensor.outside_particulate_matter_2_5um_concentration
    name: Outside
    color: '#4f4f4f'
    show:
      in_header: true
    group_by:
      func: avg
      duration: 15min
  - entity: sensor.airnow_pm2_5
    name: AirNow
    color: '#8f8f8f'
    show:
      in_header: true
    group_by:
      func: avg
      duration: 15min
card_mod:
  style: |
    ha-card {
      background: none;
      box-shadow: none; 
    }
    #state__name {
      color: #2f2f2f !important;
      /* font-weight: 700 !important; */
      font-size: 14px !important;
    }
    #header__title {
      font-size: 18px !important;
    }
```

<img width="393" src="https://user-images.githubusercontent.com/164192/168655333-ce1bbcc6-87c7-4ce8-a6a3-b750f42196ab.png">

```yaml
type: custom:apexcharts-card
graph_span: 1d
show:
  loading: false
update_interval: 5min
apex_config:
  xaxis:
    tooltip:
      enabled: false
  grid:
    borderColor: grey
  chart:
    height: 160px
  yaxis:
    min: 0
    tickAmount: 4
header:
  show: true
  show_states: true
span:
  end: hour
  offset: '-1hr'
all_series_config:
  show:
    legend_value: false
  type: column
  float_precision: 2
  group_by:
    func: last
    duration: 1hr
series:
  - entity: sensor.energy_hourly
    name: Energy Hourly
    color: '#8f8f8f'
card_mod:
  style: |
    ha-card {
      background: none;
      box-shadow: none;      
    }
    #state__name {
      color: #2f2f2f !important;
      /* font-weight: 700 !important; */
      font-size: 16px !important;
    }
```

## Entities Card

With [multiple-entity-row](https://github.com/benct/lovelace-multiple-entity-row).

<img width="394" src="https://user-images.githubusercontent.com/164192/168655614-5cdf03e4-f171-45b3-aed4-5854fabd97bb.png">

```yaml
type: entities
entities:
  - type: custom:multiple-entity-row
    entity: sensor.total_power
    name: Power (W)
    state_header: Current
    format: precision0
    unit: ' '
    entities:
      - entity: sensor.daily_power_min
        format: precision0
        name: Min
        unit: ' '
      - entity: sensor.daily_power_max
        name: Max
        format: precision0
        unit: ' '
      - entity: sensor.daily_power_average
        name: Avg
        format: precision0
        unit: ' '
card_mod:
  style: |
    ha-card {
      background: none;
      box-shadow: none;
      filter: grayscale(1);
      zoom: 1.2;
    }
    #states {
      /*font-weight: 700 !important; */
      color: #2f2f2f;
      font-size: 16px;
      padding: 2px;
    }
```

<img width="596" src="https://user-images.githubusercontent.com/164192/168656051-efecc73e-cea9-42df-a4ca-a02436c31aad.png">

```yaml
type: entities
entities:
  - entity: sensor.d_power
    name: Power
footer:
  type: graph
  entity: sensor.d_power
  hours_to_show: 24
  detail: 2
card_mod:
  style: |
    ha-card {
      background: none;
      box-shadow: none;
      filter: grayscale(1);
    }
    #states {
      /*font-weight: 700 !important; */
      color: #2f2f2f;
      font-size: 16px;
      padding: 2px;

    }
    .card-content{
      zoom: 1.8;
    }
```
