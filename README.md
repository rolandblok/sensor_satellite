# sensor satellite

A small **tabletop electronic sculpture**: an ESP32-C3 reads temperature,
humidity and pressure, and renders them to a 2.9" e-paper panel that holds its
image without power. It runs from a solar panel and a supercapacitor, with no
battery and no maintenance.

**It lives indoors.** That is the design case, not a bench convenience — see
[solar_node.md](solar_node.md), where it is the single biggest constraint on the
energy budget.

The e-paper is the reason the energy budget works — it draws power only during
a refresh, and nothing at all while displaying.

## There is no PCB

**The wiring is the object.** This is a tabletop sculpture of a satellite, and
the connections are made as **3D wire bends** — shaped, free-standing runs that
hold the modules in place and form the structure. There is no circuit board, and
there is not going to be one.

[`seed_mini_drawing.svg`](seed_mini_drawing.svg) is the drawing of those wire
traces: an A4 Inkscape sheet, not a schematic and not a layout. It is the
template for what gets bent.

**It matches what is soldered**, labels and paths both, as of 2026-09-18.

Worth knowing if it is ever edited mechanically: the sheet carries two views and
they are not the same kind of drawing. The left one is the board view, whose
wires land in strict pin order down the column; the right one is the routing
view, where the landing order is shaped for the object. The first could be
redrawn from a pin map, the second cannot.

Two consequences worth carrying into the rest of these notes:

* **A schematic is not the deliverable.** The KiCad project was removed on
  2026-08-21 for this reason — see [solar_node.md](solar_node.md). The design
  notes, the drawio sheets and the pinout files are the current intent.
* **Pin assignment has a physical dimension.** Which pin a signal lands on
  decides where its wire physically runs, so electrical freedom and the shape of
  the sculpture are the same decision. [gpio_xiao.md](gpio_xiao.md) records what
  is electrically fixed and what is not.

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
memory verified. A cap-only run has been done — 4.7 V down to a 3.04 V brownout.

**Sleep current, measured 2026-09-04: 40–50 µA**, on a Seeed XIAO ESP32-C3 fed
from an HT7533 into its `3V3` pin with the onboard LED removed. Through the
XIAO's own regulator at the `5V` pin instead it is high. **The soldered build has
no HT7533** — VCAP goes to the `5V` pin and the onboard regulator makes 3V3 — so
that 40–50 µA is what an external LDO would buy, not what the sculpture draws
today.

Not yet done: the overvoltage clamp, unfitted on either board; logging to flash,
which the XIAO build needs before it can do a cap-power run at all; and the
XIAO's dropout, and so its brownout point.

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
[gpio_xiao.md](gpio_xiao.md); pin budget, module strapping (BME280 `CSB`/`SDO`, e-paper
`BS`) and board gotchas in
[proto_epaper_esp32c3.md](proto_epaper_esp32c3.md).

**There is an alternative board.** The SuperMini in hand turned out to be a
**Plus V2**, whose GPIO8 carries a WS2812B costing ~1 mA in every state, black
included, with no firmware way to switch it off. The Seeed XIAO ESP32-C3 is the
same silicon with no user LED and no pixel. It brings out 11 GPIO instead of 13
— GPIO0 and GPIO1 are missing — so the I²C bus moves to GPIO20/GPIO2. The
e-paper pins are the same on both boards. Pin map, boot and strapping reasoning
in [gpio_xiao.md](gpio_xiao.md); the matching power chain in
`solar_node_xiao.drawio`.

## Layout

```
+--------------------------------------------------+
| sensor satellite          4.21 V  ^ 34 mV    #42 |
|--------------------------------------------------|
|                                       56 % RH    |
|   21.8 C                             1019 hPa    |
|                                    dew 10.4 C    |
|--------------------------------------------------|
| min 18.2   max 23.9                         v1.1 |
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

For the XIAO the FQBN differs, **and so does the CDC value**:

```
arduino-cli compile --upload -p COM7   --fqbn esp32:esp32:XIAO_ESP32C3:CDCOnBoot=default   proto_epaper_esp32c3
```

The XIAO's board definition labels `CDCOnBoot=default` as *Enabled* and
`CDCOnBoot=cdc` as *Disabled* — the opposite of the SuperMini. Carry the wrong
one over and the board flashes, verifies and prints nothing.

### Flashing with esptool directly: do not skip `boot_app0`

Deep sleep makes the port exist for only ~5 s per `CYCLE_S`, and `arduino-cli
upload` spends longer than that starting up, so it loses the race. Calling
`esptool.exe` against pre-built images wins it. **But it must write all four
images, not three:**

```
0x0      <sketch>.ino.bootloader.bin
0x8000   <sketch>.ino.partitions.bin
0xe000   tools/partitions/boot_app0.bin     <-- easy to forget
0x10000  <sketch>.ino.bin
```

`boot_app0.bin` is the OTA-select data that tells the bootloader to run app0.
Omit it and the selection is whatever was already in flash, so the board can run
something other than what you just wrote — while still reporting a verified
hash. Diagnosed 2026-09-25 after a partial flash left the panel blinking without
drawing; rewriting all four fixed it.

### Low-power hold, and the charge trend on the panel

The node will not spend the cap when there is not enough in it. Vcap is read as
the **first** thing in `setup()`, before Serial, I²C, SPI or the panel, and the
gate decides from there:

| Vcap | What happens |
| ---- | ------------ |
| ≥ `v_resume`, default 3.80 V | normal cycling, every `cycle_s` (300 s) |
| < `v_hold`, default 3.50 V | cycling stops; one "Low Power" frame, then polls every `poll_s` (900 s) |
| < `v_floor`, default 3.25 V | hold without even drawing the frame — too close to brownout |

All five are settings that can be changed over USB without reflashing - see
*Settings, without reflashing* below.

Why it is needed: 4 F is enormous next to this load, so a failed boot costs only
**~1.5 mV**. A brownout reset therefore leaves Vcap where it was, the node
retries immediately, and it loops at ~20 mA — walking the cap down ~5 mV/s until
the chip can no longer start. Measured brownout for this configuration is
**3.04 V** (2026-08-30 cap-only run). A full cycle costs ~70 mV of the pack; a
held-off wake costs ~3 mV, which is what makes waiting affordable. The gap
between 3.50 and 3.80 V is the hysteresis, and it is what stops the node
oscillating across a single threshold.

**The header shows what the sleep gained**, right after Vcap: a drawn triangle
plus the delta in mV, with a ±20 mV deadband so ADC spread does not produce a
flickering arrow. The delta and the boot counter are in the same small font as
the version number. The triangle carries the direction, so the number is printed
unsigned; a signed number next to the flat bar used to read as a double minus.
This is deliberately the *sleep* delta, Vcap now against Vcap when the last cycle
finished, not wake-to-wake: it is harvest with this node's own consumption
excluded, which is the number that answers "is there enough light". A flat bar is
break-even; the cycle's own ~−70 mV cost is in the log, not on the panel.

Indoor harvest measured 2–4 mA at low cap voltage (2026-08-21, both panels), and
break-even for a 300 s cycle is roughly **1.0–1.5 mA**, so the trend arrow is
expected to sit near flat indoors and go negative in poor light. Neither the
awake current nor this configuration's sleep current is measured yet, so that
break-even figure is an estimate.

### The flash log

Serial goes nowhere on cap power — USB is unplugged and the GPIO20 mirror is
compiled out on this board — so every wake writes **one 16-byte record into the
`spiffs` data partition** instead. No filesystem is mounted: the partition is
used as a plain append-only array of fixed slots, one `esp_partition_write` per
wake, sectors erased lazily just before first use so no single operation costs
more than one 4 kB erase.

`spiffs` is at 0x290000 and is 1.375 MB, which is **90,111 records — about 313
days** at a 300 s cycle. A normal sketch upload does not touch that partition, so
**the log survives reflashing**; only an explicit `--erase-all` or the `e` command
clears it.

Each record holds Vcap at the top of `setup()` and Vcap immediately before sleep,
which is what makes the log answer the question it exists for:

| Derived at dump time | Meaning |
| -------------------- | ------- |
| `dv_sleep_mV` | this boot's Vcap minus the **previous** record's sleep Vcap — harvest alone, with this node's own consumption excluded |
| `dv_cycle_mV` | sleep Vcap minus boot Vcap — what the update itself cost, normally about −70 mV |
| `net_uA` | `C·dV/dt` over `slept_s`, so it stays correct across a 900 s hold poll as well as a 300 s cycle |

**`incomplete` is the column to sort on.** An attempt that never reaches its
sleep writes no record — so before the expensive part starts, the boot voltage
goes into RTC memory as a breadcrumb, and the *next* boot notices it and emits a
record flagged `LF_INCOMPLETE` with `v_sleep_mV = 0`. That turns a brownout
part-way through an update from a silence into a row saying "this starting
voltage was not enough", which is exactly what sets `VCAP_HOLD` and `VCAP_FLOOR`
empirically rather than by the reasoning above.

Held-off polls are logged too, flagged `held`, so the recovery curve during a
hold is visible and not just its endpoints. A brownout *during* a held poll is
not recorded — a poll costs ~3 mV, so reaching one means the cap was already at
the floor.

#### Reading it out: hold BOOT

**Download mode is not how you read the log.** `hold BOOT → tap RESET → release
BOOT` parks the ROM bootloader with the sketch never running, so nothing can dump
anything. That mode is for *uploading*. GPIO9 is only *sampled* at the instant
reset is released — nothing looks at it afterwards — and that gap is what
separates the two jobs:

| You want | Do this | Because |
| -------- | ------- | ------- |
| **Flash new firmware** | hold BOOT, tap RESET, release BOOT | GPIO9 low *at* reset → ROM download mode, sketch never runs |
| **Read the log** | tap RESET, **then** hold BOOT | GPIO9 high at reset → sketch runs → sketch reads GPIO9 low → console mode |

In console mode the board **prints its status and does not sleep**, so there is no
window to miss and no race to win. The way out is RESET. It is checked *before*
the hold gate, deliberately: the gate's whole job is to sleep without bringing
Serial up, and a flat cap is precisely when you most want the log.

```
# BOOT held - console mode, this board will not sleep.
# d = dump log, e = erase log, p = settings, s <name> <value> = set,
#   r = restore defaults, c = continue (cycle then sleep). Waiting 5 s...
```

The prompt re-offers itself after each command, so dump, look, erase, dump again
without resetting between.

`tools/dump_log.py` does the dump for you and saves it under `data/<date>/`:
start it, then plug USB in (jumper open) or tap RESET. Any byte opens the
cold-boot window, so there is no timing to get right.

#### Settings, without reflashing (v1.3)

The thresholds and both sleep lengths are **settings**, stored in the NVS
partition. They survive power loss and reflashing, and they are not part of the
flash log. The `#define`s are only the defaults.

| Name | Default | What |
| ---- | ------- | ---- |
| `cycle_s` | 300 | sleep between updates. Below 180 the node warns: the e-paper is not rated for refreshes that often |
| `poll_s` | 900 | sleep between held-off polls |
| `v_hold` | 3.50 | cycling stops below this |
| `v_resume` | 3.80 | and restarts at this |
| `v_floor` | 3.25 | below this, not even a hold frame |
| `park_epd` | 0 | 1 = hold the e-paper's RST/CS high and SCK/MOSI/DC low through sleep, instead of letting them float (v1.4). The S3 test in `sleep_current/` |

```
python tools/node_cfg.py                          # show
python tools/node_cfg.py cycle_s=180 poll_s=60    # change, then show
python tools/node_cfg.py --defaults               # back to the #defines
```

Or by hand in the console: `p` shows them, `s v_hold 3.40` sets one, `r`
restores the defaults. The node refuses anything that could strand it, and says
why: it needs `v_floor < v_hold < v_resume`, all within 2.50-5.00 V,
`cycle_s >= 60` and `poll_s >= 10`. A stored set that ever fails that check is
ignored in favour of the defaults.

They are read from flash on every cold boot and kept in RTC memory, so a timer
wake pays nothing for them. A change takes effect at once: the console is always
a cold boot.

**The flash log does not record which thresholds were in force**, only each
wake's `slept_s`. Note any change you make next to the dump you later read.

There is also a **passive window on every cold boot**, if you would rather not
touch BOOT: plug USB in with the jumper open (that is a power-on) and the sketch
waits up to 2 s for either the terminal to assert DTR **or any byte to arrive** —
two triggers, because not every terminal asserts DTR, so mashing a key while it
boots always works. A timer wake never offers it; nobody is listening, and the
check costs nothing when no host is there.

### Deep sleep costs you casual reflashing

With `USE_DEEP_SLEEP 1` the node is awake for about 9 s out of every `CYCLE_S`
(300 s by default) — a 3% duty cycle. For the rest of it **COM5 does not
exist**. The C3 has no USB-serial chip; the port is the on-chip USB-Serial-JTAG
peripheral, and deep sleep unpowers it. `arduino-cli board list` shows only the
logger's COM3.

That breaks the normal upload, which opens the port and pulses DTR/RTS to force
download mode (the `Hard resetting via RTS pin` at the end of a successful
flash). No port, nothing to open, nothing to reset.

Two ways round it:

**BOOT button — deterministic.** GPIO9 is a **strapping pin, sampled once, at
the instant the chip leaves reset**. Nothing polls it afterwards, so holding it
while the board is already running does nothing at all. It only counts if a
reset happens while it is held:

```
hold BOOT  ->  tap RESET  ->  release RESET  ->  release BOOT
```

The ROM then sits in download mode indefinitely with the port enumerated and the
sketch never running, so an upload cannot lose. Both buttons are on the XIAO.

**Seeed's documented procedure — "hold BOOT and connect to the PC" — does not
work on this build.** It assumes USB is the power source, so plugging in *is* the
power-on. Here the board runs off the supercap at the `5V` pin, with USB
unplugged and the jumper closed, so USB appearing or disappearing never resets
anything and the strapping is never sampled. If the RESET button is unreachable
inside the sculpture, briefly open the VCAP jumper instead — that is the
reset.

On the older cap-powered SuperMini rig the equivalent is pulling the VCAP wire
off `5V` first, for the same reason.

**Catch the wake window.** Less fragile than the duty cycle suggests: you only
have to win the first instant, because once esptool opens the port and asserts
reset the sketch is gone and the pending sleep never happens. Poll for the port
and fire the upload the moment it appears.

**But not while the node is held off.** A held-off wake never calls
`Serial.begin()` — that is the point of it — so it is ~0.3 s of ADC read every
`HOLD_S` (900 s) and the port never enumerates at all. There is no window to
catch. **Below 3.50 V, BOOT + RESET is the only way in**, and if the cap is above
`VCAP_FLOOR` the panel will be showing the LOW POWER frame, which is how you can
tell from across the room which case you are in.

Plugging USB in with the jumper open is also a power-on. Above `VCAP_FLOOR` a
cold boot runs one full cycle regardless of the hysteresis, so a human pressing
RESET or applying power gets a frame and a log line - a ~9 s window even on a
weak cap. Do not lean on it repeatedly at low voltage: each press costs a full
cycle plus, on the next timer wake, another hold frame - roughly 90 mV of the
pack.

**Below `VCAP_FLOOR` a cold boot sleeps at once - unless a USB host is there.**
The divider is on the cap side of the jumper, so on USB it still reads the cap.
Up to v1.1 that meant a USB plug-in with a flat cap slept before `Serial` came
up and then held forever: no port, no frame, a board that looked dead on USB
(2026-10-03). From v1.2 a cold boot first looks for USB SOF packets for up to
0.5 s and skips both gates if it finds them; a power bank sends none and does
not count. Only cold boots check, so USB plugged into a board that is already
asleep and holding needs a RESET.

Either way, **reflashing wipes RTC memory** — `bootCount` returns to 0,
`tMin`/`tMax` reset, the hold latch clears, and `vcapPrev` is gone so the first
frame after a flash has no trend arrow. Change `CYCLE_S` before starting a
discharge run, not partway through one.

## Contents

| Path | What |
| ---- | ---- |
| [`proto_epaper_esp32c3/`](proto_epaper_esp32c3/) | current firmware — BME280 + e-paper |
| [`proto_oled_esp32c3/`](proto_oled_esp32c3/) | sensor-only build, serial logging, no display |
| [`i2c_scan/`](i2c_scan/) | I²C scanner; sweeps every pin pair to find the bus |
| [`logger_d1_mini/`](logger_d1_mini/) | D1 mini witness logger — relays the node's log on cap power, second Vcap ADC, OLED readout |
| [`gpio_xiao.md`](gpio_xiao.md) | Seeed XIAO ESP32-C3 pinout and this build's pin map — the single truth for pins |
| [`solar_node_xiao.drawio`](solar_node_xiao.drawio) | power chain as built: TL431 clamp, VCAP into the `5V` pin, Vcap sense divider |
| [`seed_mini_drawing.svg`](seed_mini_drawing.svg) | the wire traces to bend — A4 Inkscape sheet, the build template; matches what is soldered |
| [`project.md`](project.md) | original design concept |
| [`proto_epaper_esp32c3.md`](proto_epaper_esp32c3.md) | current build: wiring, firmware, bring-up |
| [`proto_oled_d1_mini.md`](proto_oled_d1_mini.md) | earlier ESP8266 bench rig and its power analysis |

## Licence

Public domain — [The Unlicense](UNLICENSE). Do whatever you like with it.
