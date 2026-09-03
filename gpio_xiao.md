# Seeed XIAO ESP32-C3 — pinout

**This is the alternative board, not the one the build currently runs on.** The
node is built on an ESP32-C3 SuperMini; see [gpio.md](gpio.md) for that. This
file exists because the SuperMini in hand turned out to be a **Plus V2**, whose
GPIO8 WS2812B costs ~1 mA in every state and cannot be switched off in firmware.
The XIAO is the same silicon with none of that.

Same chip — ESP32-C3, single RISC-V core, 4 MB flash, native USB, no serial
chip. Different board: 14 pins instead of 16, **11 GPIO instead of 13**, a u.FL
antenna connector instead of a ceramic antenna, and a LiPo charger on the
underside.

Viewed from the top, component side up, USB-C at the top:

```
                                 /--USB-C--\
  ADC1_2  strap  D0  GPIO2   ----|         |---- 5V      VBUS + charger in
  ADC1_3         D1  GPIO3   ----|         |---- GND
  ADC1_4         D2  GPIO4   ----|         |---- 3V3     LDO out, 700 mA
  ADC2_0         D3  GPIO5   ----|         |---- GPIO10  D10  MOSI
          SDA    D4  GPIO6   ----|         |---- GPIO9   D9   MISO, BOOT, strap
          SCL    D5  GPIO7   ----|         |---- GPIO8   D8   SCK, strap
          U0TXD  D6  GPIO21  ----|         |---- GPIO20  D7   U0RXD
                                 | [u.FL]  |
                                 \---------/
```

**GPIO0 and GPIO1 are not bonded out.** That is the whole difference from the
SuperMini and the only thing that forces any rework — they are the current I²C
bus. GPIO11–GPIO17 are flash, GPIO18/GPIO19 are USB D−/D+, same as always.

## Per pin

| Pin | Silk | ADC | Alternate | Notes |
| --- | ---- | --- | --------- | ----- |
| GPIO2 | D0 | ADC1_2 | — | **strapping** — must be HIGH at boot |
| GPIO3 | D1 | ADC1_3 | — | free, safe |
| GPIO4 | D2 | ADC1_4 | — | free, safe |
| GPIO5 | D3 | ADC2_0 | — | free; ADC2 is nominally unusable with WiFi, but see the gotchas |
| GPIO6 | D4 | — | SDA | Arduino's default SDA on this board |
| GPIO7 | D5 | — | SCL | Arduino's default SCL on this board |
| GPIO8 | D8 | — | SCK | **strapping** — must be HIGH at boot. **No LED here.** |
| GPIO9 | D9 | — | MISO | **strapping** — BOOT button, external pull-up; LOW at boot enters download mode |
| GPIO10 | D10 | — | MOSI | free, safe, no ADC |
| GPIO20 | D7 | — | U0RXD | UART0 RX by default; quiet through reset |
| GPIO21 | D6 | — | U0TXD | UART0 TX; the ROM boot log prints here at every reset |

**GPIO8 carries nothing.** No blue LED, no RGB pixel — Seeed's wiki is explicit
that "there is no LED_BUILTIN available for the XIAO ESP32C3". The only LED on
the board is **CHG**, tied to the battery charger. That is the entire reason
this file exists.

Published deep sleep is **43–44 µA**, against the 0.6–1.5 mA the Plus V2 is
stuck at. Seeed does not state where that is measured, so treat it exactly as
this project treats the SuperMini's "40–100 µA" — a datasheet estimate awaiting
a shunt.

## Power pins

| Pin | What it is |
| --- | ---------- |
| `5V` | VBUS, and also the **charge IC's input**. Feeding it back-feeds a connected USB host. |
| `3V3` | Onboard LDO output, 700 mA. Can be fed directly to bypass the regulator, but only below 3.6 V. |
| `GND` | One pad. |

There are also **B+ / B− pads on the underside** for a single-cell LiPo, with a
charge IC rated 380 mA fast / 40 mA trickle. This build does not use them. See
*The charger is on the 5V pin* in the gotchas — it is not free just because
nothing is plugged into it.

## As wired in this build

The external LDO is **skipped**: VCAP feeds `5V` and the onboard regulator makes
3V3. See [solar_node.md](solar_node.md) for what that costs.

```
                                 /--USB-C--\
  BME280 SCL ------- GPIO2   ----|         |---- 5V     ---- VCAP (USB unplugged)
  VSENSE, Vcap tap - GPIO3   ----|         |---- GND    ---- common ground
  e-paper CLK ------ GPIO4   ----|         |---- 3V3    ---- BME280 + e-paper VCC
  e-paper RST ------ GPIO5   ----|         |---- GPIO10 ---- e-paper BUSY
  e-paper DIN ------ GPIO6   ----|         |---- GPIO9  ---- free (BOOT button)
  e-paper CS  ------ GPIO7   ----|         |---- GPIO8  ---- free
  e-paper DC  ------ GPIO21  ----|         |---- GPIO20 ---- BME280 SDA
                                 | [u.FL]  |
                                 \---------/
```

| Device | Signal | C3 pin | Silk | Moved? |
| ------ | ------ | ------ | ---- | ------ |
| BME280 | SCL | GPIO2 | D0 | **yes**, from GPIO1 |
| Vcap divider | VSENSE tap | GPIO3 | D1 | no |
| e-paper | CLK | GPIO4 | D2 | no |
| e-paper | RST | GPIO5 | D3 | no |
| e-paper | DIN | GPIO6 | D4 | no |
| e-paper | CS | GPIO7 | D5 | no |
| e-paper | BUSY | GPIO10 | D10 | no |
| e-paper | DC | GPIO21 | D6 | no |
| BME280 | SDA | GPIO20 | D7 | **yes**, from GPIO0 |
| — | free | GPIO8, GPIO9 | D8, D9 | — |

Nine signals into eleven pins, two spare. **Only the I²C bus moves.** In
firmware that is `FORCE_SDA 20` and `FORCE_SCL 2`; on the bench it is two
jumpers. Everything else keeps the pin it has today.

### This assumes the GPIO20 log mirror is gone

The SuperMini build spends a pin on a UART mirror to the D1 mini witness logger,
because `Serial` is native USB-CDC and dies when USB is unplugged. **That is what
frees GPIO20 for SDA.**

The board has eight non-strapping pins — GPIO3, 4, 5, 6, 7, 10, 20, 21 — and
three strapping pins. Keep the mirror and it is ten signals into eight, so two
land on strapping pins. Drop it and it is nine into eight, so exactly one does,
and that one is SCL on GPIO2 for the reasons below.

The replacement is logging to the C3's own flash, which
[solar_node.md](solar_node.md) already proposes for an unrelated reason: no wire
crosses to a USB-powered board, so the ground loop that wrecked the 2026-09-03
shunt measurement cannot form. Two problems, one change. Do it on the SuperMini
first — it is independent of the board.

### Why SCL on GPIO2 and SDA on GPIO20

Both choices are forced, and both matter.

**SCL, not SDA, goes on the strapping pin.** GPIO2 must be high at reset. An
idle I²C bus is high, so the bus pull-up satisfies the strapping requirement for
free — but only SCL is safe there. SCL is master-driven; nothing but a short can
hold it low. SDA can be held low by a slave hung mid-transaction through a reset,
and a strapping pin held low is a board that will not boot. That is the same
"dead board, not a bad reading" failure that moved VSENSE off GPIO2 on the
SuperMini.

The hung-slave case does not disappear, it just stops being fatal: SDA on GPIO20
leaves the bus wedged instead of the board dead. Recoverable in software — nine
clock pulses on SCL with SDA released, before `Wire.begin()`.

**Do not rely on internal pull-ups for strapping.** The strapping latch happens
at the rising edge of reset, before any software runs, and the internal pulls are
not dependable in that window. What makes GPIO2 safe here is the *external* bus
pull-up on the BME280 breakout — a few kΩ, far stronger, and unambiguous.

**SDA on GPIO20, not GPIO21.** GPIO20 is U0RXD, an input, silent through reset.
GPIO21 is U0TXD and the ROM bootloader prints its boot log there at every reset.
A C3-driven output tolerates that wiggle, which is why DC lives on GPIO21 — on
both boards, for the same reason. An input that a peripheral also drives does
not: the ROM would be driving the pad against the peripheral. That is why BUSY
must never land there either.

## Boot and wake

**Wake asks nothing of any pin.** The wake source is the RTC timer
(`esp_sleep_enable_timer_wakeup`). No EXT0/EXT1 or GPIO wake is configured, so no
pin needs to be at any level to wake, none can block a wake, and none can trigger
a spurious one. That is a property of the sketch, not the board, and it carries
over unchanged.

**Boot** needs the three strapping pins high at reset:

| Pin | Carries | Held high by |
| --- | ------- | ------------ |
| GPIO2 | SCL | the I²C bus pull-up, continuously — power-on, sleep and wake alike |
| GPIO8 | nothing | unconnected, stock configuration |
| GPIO9 | nothing | the BOOT button's external pull-up, stock |

**The real hazard is the hold latches, not the boot levels.** `gpio_hold_en()`
state lives in the RTC domain, survives deep-sleep wake by design, and survives a
reflash until cleared — `unparkPins()` in the sketch exists for exactly that.
Two things need attention when the constants change:

* The held list becomes `{8, 2, 20, 2, 10}`. GPIO2 appears twice, once as itself
  and once as `FORCE_SCL`. Harmless — `gpio_hold_dis()` is idempotent — but it
  reads as a bug.
* `parkPins()` does `pinMode(2, INPUT_PULLUP); gpio_hold_en(2)`. Still correct,
  since SCL idles high, but the comment calling GPIO2 "unconnected by design" is
  no longer true.

## Migrating from the SuperMini

| | SuperMini | XIAO |
| --- | --------- | ---- |
| GPIO broken out | 13 (GPIO0–10, 20, 21) | **11** (no GPIO0/GPIO1) |
| ADC1 channels | GPIO0–GPIO4 | GPIO2, GPIO3, GPIO4 |
| GPIO8 | blue LED, or a WS2812B on Plus V2 | nothing |
| Always-on LEDs | power LED, plus the pixel on Plus V2 | CHG only, and only while charging |
| Published deep sleep | 40–100 µA (0.6–1.5 mA on Plus V2) | 43–44 µA |
| Antenna | ceramic, onboard | u.FL, external |
| Extras | — | LiPo charger, B+/B− pads |
| Arduino I²C default | SDA 8 / SCL 9 | SDA 6 / SCL 7 |
| Arduino SPI default | SCK 4 / MISO 5 / MOSI 6 / SS 7 | SCK 8 / MISO 9 / MOSI 10 |

Firmware changes, in full:

```c
#define FORCE_SDA  20      // was 0
#define FORCE_SCL  2       // was 1
```

plus deleting the `Serial0` mirror and its `Serial0.begin()`, and fixing the
`parkPins()` / `unparkPins()` pin lists and comments noted above. The e-paper
and VSENSE constants do not change.

## Gotchas

**The charger is on the `5V` pin.** VCAP feeds `5V`, and so does the charge IC's
input. With nothing on B+/B− it should be quiescent, but Seeed publishes no
figure and this project's whole budget is tens of µA. **Measure the board's sleep
current injected at `5V` before trusting anything** — the published 43 µA may
well be a `3V3` or battery-pad number that excludes both the charger and the
onboard regulator.

**`Serial` still prints nothing until USB CDC is on.** Native USB, no serial
chip, same as the SuperMini. Build with `CDCOnBoot=cdc`.

**Arduino's SPI default moved.** On this board it is SCK 8 / MISO 9 / MOSI 10,
not the C3's IO-MUX default of 4/5/6/7. The sketch binds SPI explicitly —
`SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS)` — so this changes nothing, but keep
the `-1`: it is what stops MISO being mapped onto a pin something else is using.

**ADC2 on GPIO5 is arguably usable here.** ADC2 is unusable *with WiFi*, and this
node never initialises the radios. It is not something to rely on, but it is a
fourth analogue channel in reserve if a future sensor needs one.

**No onboard antenna.** The u.FL connector needs the supplied antenna fitted
before the radios will work. Irrelevant while WiFi and BLE stay unpowered, and
worth remembering the day that changes.

**The pinout silk is D-numbers, the datasheet and code are GPIO numbers.** They
do not correspond in any regular way — D6 is GPIO21 and D7 is GPIO20, which are
adjacent on the chip and on opposite sides of the board. Wire from the GPIO
column of the table above, not the silk.

Power chain, the TL431 clamp and the skipped regulator are in
[solar_node.md](solar_node.md) and `solar_node_xiao.drawio`.
