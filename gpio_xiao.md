# Seeed XIAO ESP32-C3 — pinout

**This is the board, and the only board in the design.** Flashed and measured
since 2026-09-04, soldered into the sculpture since 2026-09-18. This file exists
because the ESP32-C3 SuperMini it replaced turned out to be a **Plus V2**, whose
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
the board is **CHG**, tied to the battery charger, and **it has been desoldered**
(2026-09-04). That is the entire reason this file exists.

Published deep sleep is **43–44 µA**, against the 0.6–1.5 mA the Plus V2 is stuck
at — and on the bench, fed through an HT7533 with the LED removed, this board
measured **40–50 µA asleep**, so the published figure holds up. **The soldered
build has no HT7533** and runs on the onboard regulator, which costs far more;
see *Power pins* below.

## Power pins

| Pin | What it is |
| --- | ---------- |
| `5V` | VBUS, and also the **charge IC's input**. **This is where VCAP feeds the board**, through a jumper — and feeding it back-feeds a connected USB host, so the jumper and USB are never closed together. |
| `3V3` | Onboard LDO output, 700 mA — an output here, as designed. Max 3.6 V if an external regulator ever drives it instead. |
| `GND` | One pad. |

There are also **B+ / B− pads on the underside** for a single-cell LiPo, with a
charge IC rated 380 mA fast / 40 mA trickle. No battery is fitted — but feeding
the `5V` pin does put the charge IC's input on the supercap rail. See the
gotchas.

### No external LDO — and what fitting one would buy

**No HT7533 is fitted.** VCAP feeds the `5V` pin through a jumper and the onboard
regulator makes 3V3. That is the board as soldered, and it is the baseline.

It is not the low-power configuration, and the gap is measured. On 2026-09-04,
fed at the `5V` pin through the onboard regulator, consumption was high; refitted
with an **HT7533 into the `3V3` pin**, onboard LED removed, the same board slept
at **40–50 µA** — Seeed's published figure, so essentially the whole excess was
the onboard regulator and the LED rather than the C3.

So the HT7533 is the upgrade to reach for if sleep current becomes the binding
constraint, and it is worth an order of magnitude. Fitting it means driving `3V3`
directly, which is only safe *because* a regulator is in front of it: VCAP runs
4.6 V down to ~3.0 V and the pin tops out at 3.6 V. Design note 8b in
[solar_node.md](solar_node.md).

## As wired in this build

VCAP feeds the `5V` pin through a jumper, and the onboard regulator makes `3V3`
for the C3, the BME280 and the e-paper. There is no external regulator.

```
                                    /--USB-C--\
  BME280 SCL ------ D0  GPIO2   ----|         |---- 5V           <---  VCAP, via jumper
  VSENSE tap ------ D1  GPIO3   ----|         |---- GND          ----  common ground
  e-paper CLK ----- D2  GPIO4   ----|         |---- 3V3          --->  onboard LDO out,
                                    |         |                        BME280 + e-paper VCC
  e-paper RST ----- D3  GPIO5   ----|         |---- GPIO10  D10        free - was BUSY, pad broke
  e-paper DIN ----- D4  GPIO6   ----|         |---- GPIO9   D9         free (BOOT button)
  e-paper CS ------ D5  GPIO7   ----|         |---- GPIO8   D8         free
  e-paper DC ------ D6  GPIO21  ----|         |---- GPIO20  D7   ----  BME280 SDA
                                    | [u.FL]  |
                                    \---------/
```

| Device | Signal | C3 pin | Silk | vs. the SuperMini |
| ------ | ------ | ------ | ---- | ----------------- |
| BME280 | SCL | GPIO2 | D0 | **moved**, from GPIO1 |
| Vcap divider | VSENSE tap | GPIO3 | D1 | same |
| e-paper | CLK | GPIO4 | D2 | same |
| e-paper | RST | GPIO5 | D3 | same |
| e-paper | DIN | GPIO6 | D4 | same |
| e-paper | CS | GPIO7 | D5 | same |
| e-paper | DC | GPIO21 | D6 | same |
| e-paper | ~~BUSY~~ | — | — | **not connected** — see below |
| BME280 | SDA | GPIO20 | D7 | **moved**, from GPIO0 |
| — | free | GPIO8, GPIO9, GPIO10 | D8, D9, D10 | — |

Eight signals into eleven pins, three spare. Two differences from the SuperMini:
the I²C bus, because GPIO0 and GPIO1 are not bonded out here, and BUSY, which is
not wired at all.

### BUSY is not connected — the pad broke, 2026-09-18

The panel's BUSY pad lifted and took a length of trace with it. It was **not
repaired**: `EPD_BUSY` is `-1`, so GxEPD2 stops polling and falls back on fixed
worst-case delays. For this panel those are tuned close to measured — power-on
100 ms against 95.9, full refresh 4100 against 4012, power-off 150 against
140.4 — so a refresh costs ~4350 ms blind against ~4248 ms polled. **The whole
price is ~100 ms of extra awake time per cycle, under 2% of the burst.**

Leaving it defined as GPIO10 with the pad broken would have been far worse. The
pin floats, and `_busy_level` is HIGH with a 10 s `_busy_timeout`: read HIGH and
every wait burns the full 10 s — three per refresh, so ~30 s awake instead of
4.3 — while read LOW returns instantly and clocks commands into a panel that is
still working.

What is given up: a stuck panel can no longer be detected, and the ~2% margin on
the refresh delay is thin only if the panel runs slow, which it does when cold.
This is an indoor sculpture, so that does not arise. **GPIO10 / D10 is now free**,
though `parkPins()` still pulls it down and latches it so the unconnected pin
does not float in sleep.

### A wire-bend reorder was designed and then not built

Worth recording, because the reasoning is still sound and may come back. On
2026-09-11 the e-paper pins were reordered so the panel's own header sequence —
DIN, CLK, CS, DC, RST — ran straight down D2 to D6, putting five wires parallel
instead of crossing. There is no PCB here, so wire geometry is worth more than
electrical tidiness, and it costs nothing electrically: the C3's GPIO matrix
makes SPI pins free to place, and routing through the matrix rather than the
IO-MUX caps the clock near 40 MHz against the panel's ~4 MHz.

**It was superseded by the soldering iron.** The build was wired to the original
order on 2026-09-18, which is electrically fine and is what the table above now
records. The cost is only that the runs cross rather than lie parallel.

The one constraint that survives either way is the next section, and the as-built
wiring satisfies it: **DC is on GPIO21, not RST.**

### Why RST cannot take GPIO21, and DC can

GPIO21 is UART0 TX, and **the ROM bootloader prints its boot log there at every
reset — including every deep-sleep wake.** A few milliseconds of 115200 baud, so
a burst of ~8.7 µs low periods on the pin before the sketch starts.

RST on the SSD1680 is active LOW. Put it there and the panel takes dozens of
short reset pulses every wake, *while it is hibernating*. It would still draw
correctly — `GxEPD2::init()` does a proper hardware reset well afterwards — so
this is not a functional fault. It is a power fault: the pulses drag the panel
out of deep sleep into an undefined state for the ~300 ms until `init()` runs,
every single cycle, and it would surface later as a sleep-current figure nobody
could explain. In a project whose headline number is tens of microamps, that is
the expensive kind of harmless.

DC is the signal that belongs there. The C3 drives it, the panel never does, and
nothing latches it — the boot-log wiggle is over long before `init()` touches the
panel. **BUSY is the other pin that must never go there**, and for a different
reason: the panel drives BUSY, so the ROM would be driving the pad against it.

For completeness, **DIN would also be safe on GPIO21**. The panel only latches
data on CLK edges with CS low, and CLK is idle through the boot log, so garbage
on DIN is ignored. That would allow the reversed run RST, DC, CS, CLK, DIN down
D2 to D6 — useful only if the panel ends up physically flipped, which is a choice
the sculpture makes, not the electronics.

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
| Always-on LEDs | power LED, plus the pixel on Plus V2 | CHG only — **desoldered here** |
| Published deep sleep | 40–100 µA (0.6–1.5 mA on Plus V2) | 43–44 µA |
| Antenna | ceramic, onboard | u.FL, external |
| Extras | — | LiPo charger, B+/B− pads |
| Arduino I²C default | SDA 8 / SCL 9 | SDA 6 / SCL 7 |
| Arduino SPI default | SCK 4 / MISO 5 / MOSI 6 / SS 7 | SCK 8 / MISO 9 / MOSI 10 |

**The firmware already carries both.** `proto_epaper_esp32c3.ino` has a
board switch at the top of the pin map:

```c
#define BOARD_XIAO      0      // 0 = ESP32-C3 SuperMini (what this build runs on)
                               // 1 = Seeed XIAO ESP32-C3
```

Setting it to `1` moves the bus to `FORCE_SDA 20` / `FORCE_SCL 2`, compiles the
`Serial0` mirror out via `USE_LOG_MIRROR`, drops GPIO2 from the `parkPins()`
list (it is `FORCE_SCL` there, so it would otherwise be parked twice), and swaps
the bus-discovery candidate list for one without GPIO0/GPIO1.

**The e-paper pins are not board-conditional** — both boards are wired the same:

```c
#define EPD_SCK   4            // XIAO D2  CLK
#define EPD_RST   5            // XIAO D3
#define EPD_MOSI  6            // XIAO D4  DIN
#define EPD_CS    7            // XIAO D5
#define EPD_DC    21           // XIAO D6
#define EPD_BUSY  -1           // was GPIO10 / XIAO D10 - pad broke, not repaired
#define EPD_MISO  -1
```

They were briefly split for the wire-bend reorder described above, and merged
again on 2026-09-18 once the board was soldered to the original order.

Both configurations compile clean, and **note the different `CDCOnBoot` value** —
see the gotchas, this is not a typo:

```
arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc          proto_epaper_esp32c3
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C3:CDCOnBoot=default proto_epaper_esp32c3
```

**Flashed to a XIAO on COM7, 2026-09-04.** The upload verifies. Bring-up beyond
that is not finished — see the gotchas for why an apparently silent board is
usually a reading-window problem rather than a dead one.

**And the XIAO build has no log once USB is unplugged.** `USE_LOG_MIRROR 0`
leaves only USB-CDC, so it is bench-usable but cannot do a cap-power run until
the flash logger exists. That part is not written.

## Gotchas

**The charger sits on the `5V` pin, and this build feeds that pin.** The charge
IC's input is on `5V` alongside VBUS, so the supercap sees it with no battery on
B+/B− to charge. Seeed publishes no quiescent figure for it, and the 40–50 µA
measurement does not bound it — that run fed `3V3` through an HT7533 and bypassed
the charger along with the onboard regulator.

**Never close the VCAP jumper with USB plugged in.** The `5V` pin is VBUS, so a
charged supercap drives the host's 5 V rail and the host charges the cap back
through the same wire. Open the jumper to flash, close it to run. If an HT7533
into `3V3` is ever fitted, the equivalent hazard is two regulators in parallel on
the `3V3` node, the higher one winning and the other seeing reverse.

**The `CDCOnBoot` values are INVERTED against the SuperMini's board definition.**
This one costs an afternoon. `arduino-cli board details` for each:

```
esp32:esp32:esp32c3         CDCOnBoot=cdc      -> Enabled
esp32:esp32:XIAO_ESP32C3    CDCOnBoot=default  -> Enabled   (the board default)
                            CDCOnBoot=cdc      -> Disabled
```

The same option string means opposite things. Carrying `CDCOnBoot=cdc` over from
the SuperMini command line **disables** CDC on the XIAO, and since this build
also compiles the GPIO20 mirror out, `logBoth()` is left with no output path at
all — the board runs, flashes and verifies normally and prints absolutely
nothing. It looks dead and is not. Use:

```
arduino-cli compile --upload -p COM7   --fqbn esp32:esp32:XIAO_ESP32C3:CDCOnBoot=default   proto_epaper_esp32c3
```

**And CDC discards output when no host holds the port.** The setup burst goes out
~300 ms after reset; open the port a second later and it is gone, with no way to
get it back. Combined with `loop()` delaying on `CYCLE_S`, a port opened
mid-cycle reads zero bytes for up to 300 s and looks exactly like a hung board.
Open the port *first* and hold it, or catch the wake window.

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

**Pin choice is also a mechanical decision.** There is no PCB — the connections
are 3D wire bends that form the structure of the sculpture — so moving a signal
to a different GPIO moves where its wire physically runs. Anything electrically
free above is free to be routed for the shape as well, which is exactly what the
e-paper ordering does.

**`seed_mini_drawing.svg` matches the build, with one exception.** Labels and
red paths both read DIN→D4, CLK→D2, CS→D5, DC→D6, RST→D3, BUSY→D10. The first
five are what is soldered; **the BUSY→D10 wire is drawn but does not exist** —
that pad broke on 2026-09-18 and was not repaired. The drawing has not been
changed for it, because the trace is still the right one to draw if the panel is
ever replaced.

It was relabelled for the 2026-09-11 pin reorder and reverted on 2026-09-18 when
the board was built to the original order; because the paths were never
rerouted, reverting the labels made it self-consistent again.

Power chain, the TL431 clamp and the skipped regulator are in
[solar_node.md](solar_node.md) and `solar_node_xiao.drawio`.
