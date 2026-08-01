# sensor satellite

A low-power outdoor environmental sensor node: an ESP32-C3 reads temperature,
humidity and pressure, and renders them to a 2.9" e-paper panel that holds its
image without power. Intended to run eventually from a solar panel and a
supercapacitor, with no battery and no maintenance.

The e-paper is the reason the energy budget works — it draws power only during
a refresh, and nothing at all while displaying.

```
         Sun                    ESP32-C3
          |                     /      \
   solar panel  ->  supercap  ->        \
                                BME280   e-paper
```

## Status

| Stage | State |
| ----- | ----- |
| ESP8266 bench rig | done — [proto_oled_d1_mini.md](proto_oled_d1_mini.md) |
| ESP32-C3 + BME280 + e-paper, USB powered | **current** — [proto_epaper_esp32c3.md](proto_epaper_esp32c3.md) |
| Supercapacitor + solar | not started |

Working now: sensor reads validated, CSV logging over USB serial, e-paper
driver initialising with the correct panel class. Not yet done: end-to-end
refresh on the physical panel, current measurements, deep sleep.

## Hardware

| Part | Choice |
| ---- | ------ |
| MCU | ESP32-C3 SuperMini |
| Sensor | BME280 — temperature / humidity / pressure |
| Display | Waveshare 2.9" b/w e-paper, 296×128, SSD1680 |
| Storage | 5.5 V 4 F supercapacitor *(later)* |
| Solar | 5 V ~200 mA panel *(later)* |
| Protection | 1N5819 Schottky *(later)* |

### Wiring

```
                 ESP32-C3 SuperMini
                 ------------------
  BME280  SDA ---- GPIO0                GPIO3 ---- DC    e-paper
          SCL ---- GPIO1                GPIO4 ---- CLK
          VIN ---- 3V3                  GPIO5 ---- RST
          GND ---- GND                  GPIO6 ---- DIN
                                        GPIO7 ---- CS
                                       GPIO10 ---- BUSY
                                          3V3 ---- VCC
                                          GND ---- GND
```

Full pin budget, module strapping (BME280 `CSB`/`SDO`, e-paper `BS`) and the
board-specific gotchas are in
[proto_epaper_esp32c3.md](proto_epaper_esp32c3.md).

## Layout

```
+--------------------------------------------------+
| sensor satellite                             #42 |
|--------------------------------------------------|
|                                        56 %RH    |
|   21.8 C                             1019 hPa    |
|                                    dew 10.4 C    |
|--------------------------------------------------|
| min 18.2   max 23.9                              |
+--------------------------------------------------+
```

## Build

Arduino ESP32 core 3.x. Libraries: GxEPD2, Adafruit BME280, Adafruit Unified
Sensor, U8g2.

```
arduino-cli compile --upload -p COM5 \
  --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc \
  proto_epaper_esp32c3
```

`CDCOnBoot=cdc` is required — the C3 has no USB-serial chip, and without it
`Serial` output goes nowhere.

## Contents

| Path | What |
| ---- | ---- |
| [`proto_epaper_esp32c3/`](proto_epaper_esp32c3/) | current firmware — BME280 + e-paper |
| [`proto_oled_esp32c3/`](proto_oled_esp32c3/) | sensor-only build, serial logging, no display |
| [`i2c_scan/`](i2c_scan/) | I²C scanner; sweeps every pin pair to find the bus |
| [`project.md`](project.md) | original design concept |
| [`proto_epaper_esp32c3.md`](proto_epaper_esp32c3.md) | current build: wiring, firmware, bring-up |
| [`proto_oled_d1_mini.md`](proto_oled_d1_mini.md) | earlier ESP8266 bench rig and its power analysis |
