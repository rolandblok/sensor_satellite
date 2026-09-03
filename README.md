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
| Supercapacitor + solar | **bench testing** — [solar_node.md](solar_node.md) |

Working now: sensor reads validated, CSV logging over USB serial and over the
GPIO20 mirror, e-paper refreshing on the physical panel, and deep sleep with RTC
memory verified. Not yet done: current measurements on the 3V3 rail, and a full
run on cap power with USB disconnected.

## Hardware

| Part | Choice |
| ---- | ------ |
| MCU | ESP32-C3 SuperMini — alternative: **Seeed XIAO ESP32-C3**, [gpio_xiao.md](gpio_xiao.md) |
| Sensor | BME280 — temperature / humidity / pressure |
| Display | Waveshare 2.9" b/w e-paper, 296×128, SSD1680 |
| Storage | 5.5 V 4 F supercapacitor *(later)* |
| Solar | 5 V ~200 mA panel *(later)* |
| Protection | 1N5819 Schottky *(later)* |

### Wiring

```
                 ESP32-C3 SuperMini
                 ------------------
  BME280  SDA ---- GPIO0               GPIO21 ---- DC    e-paper
          SCL ---- GPIO1                GPIO4 ---- CLK
          VIN ---- 3V3                  GPIO5 ---- RST
          GND ---- GND                  GPIO6 ---- DIN
                                        GPIO7 ---- CS
  Vcap    tap ---- GPIO3                GPIO10 --- BUSY
  divider GND ---- GND                     3V3 ---- VCC
                                           GND ---- GND
```

DC is on GPIO21, not GPIO3: GPIO3 is the one ADC1 channel left for the
supercapacitor divider, and GPIO2 — the obvious alternative — is a strapping pin
that a divider would hold low on a flat cap. Full pinout in
[gpio.md](gpio.md); pin budget, module strapping (BME280 `CSB`/`SDO`, e-paper
`BS`) and board gotchas in
[proto_epaper_esp32c3.md](proto_epaper_esp32c3.md).

**There is an alternative board.** The SuperMini in hand turned out to be a
**Plus V2**, whose GPIO8 carries a WS2812B costing ~1 mA in every state, black
included, with no firmware way to switch it off. The Seeed XIAO ESP32-C3 is the
same silicon with no user LED and no pixel. It brings out 11 GPIO instead of 13
— GPIO0 and GPIO1 are missing — so the I²C bus moves to GPIO20/GPIO2 and
everything else keeps its pin. Pin map, boot and strapping reasoning in
[gpio_xiao.md](gpio_xiao.md); the matching power chain in
`solar_node_xiao.drawio`.

## Layout

```
+--------------------------------------------------+
| sensor satellite                   4.21 V    #42 |
|--------------------------------------------------|
|                                       56 % RH    |
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

### Deep sleep costs you casual reflashing

With `USE_DEEP_SLEEP 1` the node is awake for about 5 s out of every `CYCLE_S`
(300 s by default) — a 1.7% duty cycle. For the rest of it **COM5 does not
exist**. The C3 has no USB-serial chip; the port is the on-chip USB-Serial-JTAG
peripheral, and deep sleep unpowers it. `arduino-cli board list` shows only the
logger's COM3.

That breaks the normal upload, which opens the port and pulses DTR/RTS to force
download mode (the `Hard resetting via RTS pin` at the end of a successful
flash). No port, nothing to open, nothing to reset.

Two ways round it:

**BOOT button — deterministic.** Hold BOOT (GPIO9) while power-cycling, then
release. GPIO9 low at boot puts the ROM in download mode, where it waits
indefinitely with the port enumerated and the sketch never running. Upload
normally from there. On cap power this means pulling the VCAP wire off `5V`
first and plugging USB with BOOT held — otherwise it is not a fresh power-on and
the strapping is never sampled.

**Catch the wake window.** Less fragile than the duty cycle suggests: you only
have to win the first instant, because once esptool opens the port and asserts
reset the sketch is gone and the pending sleep never happens. Poll for the port
and fire the upload the moment it appears.

Either way, **reflashing wipes RTC memory** — `bootCount` returns to 0 and
`tMin`/`tMax` reset. Change `CYCLE_S` before starting a discharge run, not
partway through one.

## Contents

| Path | What |
| ---- | ---- |
| [`proto_epaper_esp32c3/`](proto_epaper_esp32c3/) | current firmware — BME280 + e-paper |
| [`proto_oled_esp32c3/`](proto_oled_esp32c3/) | sensor-only build, serial logging, no display |
| [`i2c_scan/`](i2c_scan/) | I²C scanner; sweeps every pin pair to find the bus |
| [`logger_d1_mini/`](logger_d1_mini/) | D1 mini witness logger — relays the node's log on cap power, second Vcap ADC, OLED readout |
| [`gpio.md`](gpio.md) | ESP32-C3 SuperMini pinout and this build's pin map |
| [`gpio_xiao.md`](gpio_xiao.md) | Seeed XIAO ESP32-C3 pinout — the alternative board, and why |
| [`solar_node_xiao.drawio`](solar_node_xiao.drawio) | power chain for the XIAO variant: TL431 clamp, no external regulator |
| [`project.md`](project.md) | original design concept |
| [`proto_epaper_esp32c3.md`](proto_epaper_esp32c3.md) | current build: wiring, firmware, bring-up |
| [`proto_oled_d1_mini.md`](proto_oled_d1_mini.md) | earlier ESP8266 bench rig and its power analysis |

## Licence

Public domain — [The Unlicense](UNLICENSE). Do whatever you like with it.
