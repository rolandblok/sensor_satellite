## ESP32-C3 + BME280 + 2.9" e-paper, USB powered

Environmental sensor node: reads temperature, humidity and pressure, renders
them to an e-paper panel that holds its image without power. USB powered for
now; solar and supercapacitor are a later stage.

| Part | Choice |
| ---- | ------ |
| MCU | ESP32-C3 SuperMini |
| Sensor | BME280 — temperature / humidity / pressure |
| Display | Waveshare 2.9" b/w e-paper, 296×128, SSD1680 |
| Power | USB 5 V |
| Sketch | [`proto_epaper_esp32c3/`](proto_epaper_esp32c3/proto_epaper_esp32c3.ino) |

---

## Wiring

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

| Device | Signal | C3 pin |
| ------ | ------ | ------ |
| BME280 | SDA | GPIO0 |
| BME280 | SCL | GPIO1 |
| Vcap divider | VSENSE tap | GPIO3 |
| Logger | UART TX out | GPIO20 |
| e-paper | DC | GPIO21 |
| e-paper | CLK | GPIO4 |
| e-paper | RST | GPIO5 |
| e-paper | DIN | GPIO6 |
| e-paper | CS | GPIO7 |
| e-paper | BUSY | GPIO10 |

Both devices take **3V3** and share **GND**. E-paper needs 3.3 V on both supply
and data lines — it is not 5 V tolerant.

### Pin budget

```
GPIO0, GPIO1    I2C      BME280
GPIO3           ADC      supercapacitor divider tap
GPIO4..GPIO7    SPI      e-paper CLK, RST, DIN, CS
GPIO10          input    e-paper BUSY
GPIO21          output   e-paper DC
GPIO2           unused   strapping pin - deliberately left open
GPIO8, GPIO9    free     strapping pins, usable with care
GPIO18, GPIO19  avoid    USB
GPIO20          UART TX  log mirror to the witness logger
```

The board is effectively full. The supercapacitor divider needs an ADC1
channel, and ADC1 on the C3 is GPIO0–GPIO4 only: GPIO0/GPIO1 are the I²C bus
and GPIO4 is the SPI clock, which leaves GPIO2 and GPIO3.

**It has to be GPIO3.** GPIO2 is a strapping pin that must be high at reset,
and a divider on it holds it at Vcap ÷ 2 — so a flat cap holds it at 0 V and
the board will not boot at all. That presents as a dead board, not as a bad
reading, and bench runs start from a flat cap. GPIO2 is left unconnected.

Freeing GPIO3 pushes e-paper DC to GPIO21. That is safe: `Serial` is USB-CDC on
GPIO18/GPIO19, so UART0 is never initialised and GPIO20/GPIO21 are ordinary
GPIO. The ROM bootloader still prints its boot log on GPIO21, so DC wiggles for
a few milliseconds at reset — harmless, since GxEPD2 hardware-resets the panel
inside `init()` well after that, but it is why CS and RST stay where they are.

The divider is **1 MΩ / 1 MΩ with 100 nF at the tap**: ÷2, 2.3 µA at 4.6 V, and
50 ms settling. 100 kΩ / 100 kΩ would bleed 27 µA, comparable to the entire
sleep budget.

Anything added later should go on the existing I²C bus, which costs no pins —
BH1750 (`0x23`) for lux is the obvious candidate.

---

## Module strapping

Two settings on the breakout boards themselves. Both are one-time.

### BME280 — CSB and SDO

On a 6-pin board (`VCC GND SCL SDA CSB SDO`) the chip selects its interface at
power-up from CSB:

| Pin | Tie to | Effect |
| --- | ------ | ------ |
| CSB | 3V3 | I²C mode. Floating or low leaves it in **SPI mode**, silent at every address on every pin. |
| SDO | GND | Address `0x76`. To 3V3 gives `0x77`. Floating is indeterminate. |

4-pin boards strap both internally and need nothing.

Bare purple GY-BME280 boards are 3.3 V only — no regulator, no level shifter.
Larger blue ones usually have both. Either way, feed from 3V3.

### E-paper — BS resistor

`BS` selects SPI framing. The 0 Ω link sits at silkscreen position `0` or `1`
(the `0` printed on the part is its resistance code, not its position):

| BS | Mode | Command/data bit |
| -- | ---- | ---------------- |
| **0** | **4-line SPI** — required | On the separate DC pin, plain 8-bit SPI |
| 1 | 3-line SPI | Prepended to every byte, making 9-bit transfers |

**Keep it at 0.** GxEPD2 has no 3-line path, 9-bit framing fights the ESP32's
byte-oriented SPI peripheral, and it would only free one GPIO. Boards ship at
BS=0. To confirm, measure continuity from the `BS` pad to GND (= 0) or VCC (= 1).

---

## Board gotchas

**Serial prints nothing until USB CDC is enabled.** The C3 has no USB-serial
chip; `Serial` is native USB. Use `CDCOnBoot=cdc` in the FQBN, or in the IDE set
*USB CDC On Boot → Enabled*. Otherwise the sketch runs but looks dead.

**MISO must be disabled or it fights RST.** The C3's default SPI mapping is
`SCK=4, MISO=5, MOSI=6, SS=7`, and RST occupies GPIO5 — the default MISO. A
plain `SPI.begin()` would claim it. E-paper is write-only, so bind SPI with
MISO off:

```cpp
display.init(115200, true, 2, false);
SPI.end();
SPI.begin(EPD_SCK, -1 /* MISO */, EPD_MOSI, EPD_CS);
```

Skipping this gives an intermittently-resetting panel rather than a clean fault.

**Verify the sensor chip ID.** Many boards sold as BME280 are BMP280 with no
humidity sensor, returning a plausible constant. The firmware reads
`bme.sensorID()`: `0x60` is a real BME280, `0x58` is a BMP280.

**Deep sleep drops the USB serial port.** It disappears on sleep and
re-enumerates on wake, so terminals disconnect each cycle. Keep
`USE_DEEP_SLEEP 0` while developing.

**Check for an always-on power LED.** Many SuperMini boards fit one drawing
1–3 mA. That is 30× the target sleep budget and will dominate the solar design.
Desolder it before the battery build.

---

## Firmware

Libraries: **GxEPD2** (pulls in Adafruit GFX), **Adafruit BME280**, **Adafruit
Unified Sensor**.

| Define | Default | Meaning |
| ------ | ------- | ------- |
| `PANEL_V2` | `1` | 1 = V2 board (SSD1680). 0 = V1 (IL3820). |
| `CYCLE_S` | `300` | seconds between refreshes — keep ≥ 180 |
| `LOG_S` | `2` | serial log interval when not deep sleeping |
| `ALTITUDE_M` | `17.0` | Eindhoven, ~17 m AMSL — for sea-level pressure |
| `MIN_REFRESH_C` | `0.0` | below this the panel is skipped, image kept |
| `USE_DEEP_SLEEP` | `0` | 1 = sleep between cycles |
| `FORCE_SDA` / `FORCE_SCL` | `0` / `1` | I²C pinned; set both to `-1` to auto-detect again |
| `VSENSE_PIN` | `3` | supercapacitor divider tap |
| `VDIV_NUM` | `2.0` | divider ratio, `(R3+R4)/R4` |
| `VDIV_CAL` | `1.0149` | calibration, meter ÷ reported — fitted 2026-08-28 |
| `LOG_TX_PIN` | `20` | UART mirror of the log, for cap runs with USB out |
| `LOG_BAUD` | `115200` | must match the logger sketch |

Check the back of the panel for a **`V2` sticker** and set `PANEL_V2` to match.
V1 and V2 are pin-compatible; only the driver class differs. A wrong class gives
a blank, mirrored, or garbled screen and nothing worse.

Behaviour:

* **I²C bus is pinned.** `FORCE_SDA`/`FORCE_SCL` hold it at GPIO0/GPIO1. The
  auto-detect sweep is still there behind `-1`, but it probes GPIO0–10 and would
  drive the VSENSE tap as a bus line, so GPIO3 is excluded from the sweep list.
* **Reads Vcap in millivolts.** `analogReadMilliVolts()` averaged over 32
  samples, not raw counts — the efuse ADC calibration is doing real work at
  these levels. Reported as a `Vcap_V` column and on the panel header.
* **Logs CSV.** `#` comments, plain data lines — pastes into a spreadsheet.
* **No `<WiFi.h>`.** This node never uses WiFi or BLE. The Arduino core does not
  power the RF PHY until something calls `WiFi.*`, so not touching it leaves both
  radios unpowered. Including the header to call `WiFi.mode(WIFI_OFF)` links the
  whole stack — 985 KB flash versus 340 KB — for identical power behaviour. Add
  the explicit shutdown only if a library is introduced that starts the radio.

Build size: **28% flash, 5% RAM**.

### Build and flash

```
arduino-cli compile --upload -p COM5 \
  --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc \
  proto_epaper_esp32c3
```

The Arduino IDE bundles `arduino-cli` at
`C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe`.

### Output

```
# proto_epaper_esp32c3  boot #1
# radios never initialised - WiFi and BLE PHY unpowered
# I2C bus: SDA=GPIO0 SCL=GPIO1
# sensor ID 0x60 at 0x76  (BME280, has humidity)
# e-paper 296x128 ready (V2 / SSD1680)
# Vcap sense: GPIO3, divider x2.00, cal 1.015 -> 4.819 V now
# t_s,T_C,RH_pct,dew_C,P_station_hPa,P_sea_hPa,Tmin_C,Tmax_C,Vcap_V
0.5,24.64,52.7,14.3,1007.64,1009.67,24.6,24.6,4.817
```

Captured on the rewired board, 2026-08-28, with `VDIV_CAL` fitted — a DMM on the
cap read 4.81 V at the same moment. Every line above is also mirrored out GPIO20
to the witness logger; see [solar_node.md](solar_node.md).

### Display layout

296×128, landscape:

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

`4.21 V` is the supercapacitor voltage and `#42` the boot counter; min/max is
the temperature range. Vcap sits in the header rather than the footer so it is
drawn before the no-sensor early return — a BME280 fault still shows the cap
voltage. Boot count and min/max live in RTC memory, which survives deep sleep. That is also the mechanism partial refresh
will need: the previous frame's state must persist to redraw only what changed.

---

## Panel constraints

Hardware limits, not preferences. They shape the sleep strategy.

* **Minimum refresh interval ~180 s.** Refreshing faster degrades the panel.
  `CYCLE_S` is 300 s and must not drop below 180 s.
* **Hibernate after every refresh.** `display.hibernate()` drops the panel's
  high-voltage rails; leaving them up damages it over time.
* **Full refresh avoids ghosting.** The firmware does a full-window update each
  cycle. If partial updates are added later, force a full refresh every 10–20
  partials to clear accumulated artefacts.
* **Refresh is unreliable below 0 °C.** Relevant outdoors in Eindhoven every
  winter. Below `MIN_REFRESH_C` the firmware skips the update and keeps the
  previous image — the panel is bistable, so this costs nothing but staleness.

---

## Measurement notes

**Forced mode, not normal mode.** Normal mode converts continuously and
self-heats the die 0.5–1 °C, which it reports as ambient. The firmware uses
Bosch's weather-monitoring profile: forced mode, 1× oversampling on all three
channels, IIR filter off, one conversion per cycle. Mount the sensor away from
the board on wires; it still reads slightly high sitting next to the regulator.

**Pressure needs a sea-level correction.** Station pressure falls ~0.12 hPa per
metre, so an uncorrected reading is not comparable to a forecast:

```
p_sea = p_station / (1 - altitude/44330)^5.255
```

`ALTITUDE_M` is 17 m for Eindhoven. Accurate to ±10 m gives ~1 hPa.

**Dew point** is derived from T and RH by the Magnus-Tetens approximation
(±0.4 °C over 0–60 °C) — condensation risk, and a humidity measure that does
not swing with temperature.

**Measured noise**, one sample per 2 s in still air:

| Channel | Spread | Datasheet accuracy |
| ------- | ------ | ------------------ |
| Temperature | ±0.02 °C | ±0.5 °C |
| Humidity | ±0.3 % | ±3 % |
| Pressure | ±0.05 hPa | ±1 hPa |

That is resolution, not absolute accuracy. Good enough for trends with no
oversampling or filtering.

---

## Bring-up

1. **I²C scan** — [`i2c_scan/`](i2c_scan/i2c_scan.ino) sweeps every pin pair and
   reports what it finds. Expect `0x76` or `0x77`.
2. **Flash and check the banner** — chip ID `0x60`, and the e-paper line
   reporting 296×128.
3. **Sanity-check readings** — breathe on the sensor; humidity should jump and
   recover over ~30 s. Compare pressure to a local station.
4. **Confirm the panel** — a full refresh takes a couple of seconds and the
   image must persist after power is removed.
5. **Measure current** on the 3V3 rail, during refresh and idle. These numbers
   size the solar panel.
6. **Enable deep sleep** last. Confirm the boot counter increments and min/max
   persist, proving RTC memory works.

### Expected current

| State | Expect |
| ----- | ------ |
| Active, radios off, no refresh | 20–25 mA |
| During e-paper refresh | +10–20 mA for ~2 s |
| Idle between refreshes | ~0 mA for the panel |
| Deep sleep, SuperMini board | 40–100 µA |

Sleep current is the figure that matters. A bare C3 module sleeps at ~5 µA; the
SuperMini's regulator quiescent current is what raises it.

---

## Next

Supercapacitor and solar. Carry forward: the measured currents above, and the
GPIO3 ADC channel now carrying the cap voltage divider.
