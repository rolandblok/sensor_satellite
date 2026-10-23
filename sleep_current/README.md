# Sleep current investigation

**Question: where do the ~400 uA go while the node sleeps?**

The 2026-10-03 panel run ([analysis](../data/2026-10-03/analysis.md)) lost
~90 mV per 900 s held poll in the dark. On a 4 F pack that is
4 F x 90 mV / 900 s = **~400 uA**. That is enough to empty the pack in 2 h after
dusk, so the node cannot survive a night. But that figure is indirect, and it
lumps everything together:

| Suspect | Lives where |
| ------- | ----------- |
| onboard 3.3 V regulator, LiPo charge IC (no battery fitted) | XIAO, both fed from the `5V` pin |
| charge-status LED | XIAO, driven by the charger. Removed on the breadboard board; unknown on the sculpture |
| the C3 itself in deep sleep | XIAO |
| e-paper module in hibernate, its control lines floating while the C3 sleeps | sculpture only |
| BME280 breakout | sculpture only |
| 1M+1M divider, ~2 uA | sculpture only |
| supercap self-discharge | the 4 F pack |

## Status

**Part 1, the bare XIAO on the breadboard: done.** It draws **~10-25 uA**
asleep, fed at `5V` through the onboard regulator and charger, LED removed.
That is a sixteenth of the 400 uA. **The XIAO's own power path is not the
problem, and an HT7533 cannot buy more than those ~25 uA.** The HT7533 is not
being tested further.

**Part 1 reopened, 2026-10-11 - see *Part 1b*.** The same bare board, now
running `sleep_probe` **v3**, sleeps at **~0.59 mA**, with one sleep in ~6 at
**~3.0 mA**. Same board, same shunt, same firmware from one sleep to the
next - and the sleep current does not repeat. The two-level behaviour the
sculpture shows (340 / 860 / 1250 uA) therefore **also happens on a bare XIAO
with nothing attached**: it is in the XIAO or in the firmware, not in the
BME280, the e-paper or the wire bends. Not yet known whether v3 (vs part 1's
v1) is what raises it.

**2026-10-16 - see *Part 1c*: it is the firmware.** Measured properly (zero
with the probe tip on its own ground, clips on the shunt's leads), v3 sleeps at
0.52 or 3.2 mA, about half the sleeps high, at random and not tied to its
phases - and the same with the charger and regulator bypassed (3.3 V into
`3V3`). A minimal `sleep_min` on the same board sleeps at ~15-90 uA with **no**
high sleeps, with or without USB CDC.

**2026-10-23: the cause is `parkPins()`** - pulls latched with `gpio_hold_en` and
`gpio_deep_sleep_hold_en()` before deep sleep (N3). Node firmware **v1.5** sets
`PARK_PINS 0` and sleeps at ~45-80 uA on the bare board (N4, N5). Next: the
sculpture on v1.5.

**Part 2, the integrated sculpture: in progress.** Status 2026-10-04:

* The sculpture sleeps at **~0.86-1.0 mA** on the lab supply at 4.5 V, cap and
  panels cut off at the jumper - with the node firmware (S2, S3) **and** with
  `sleep_probe`, which never touches the BME280 or the panel (P1-P3). So it is
  **hardware**, not the node firmware. Once, briefly, it sat at ~340 uA.
* **Ruled out:** the charge LED (desoldered, no LEDs anywhere), the D8 pull-up
  (pin reads 3.3 V asleep), VSENSE and the cap (isolated, pin V 3.10 V vs
  supply 4.5 V), the e-paper control lines (worth ~0.1 mA at most, S3/P2),
  and commanding the BME280 to sleep and the e-paper into deep sleep (P3: no
  change).
* **Left:** a module's own circuitry (BME280 breakout regulator / level
  shifter; e-paper board translator / booster), **the sculpture's XIAO itself**
  - part 1 measured a different board - or a leakage path in the wire bends.
  Firmware cannot split these further: next is unsoldering one VCC wire at a
  time. See *Where this left off* at the end.
* The wake: ~9 s at ~29 mA (CH1's probe is on 10x), matching the flash log's
  65 mV per update on 4 F, so the cap really is ~4 F.

Part 2 measures the sculpture with no soldering: the VCAP jumper is the break
point, and it captures the wake as well as the sleep.

## Contents

| Path | What |
| ---- | ---- |
| `sleep_probe/sleep_probe.ino` | minimal sleep firmware, never initialises BME280 or panel. v1 (part 1, P1): parks pins like the node, 1 s awake / 60 s asleep. v3 (now): 30 s sleeps in phases - nothing / BME280 to sleep / plus e-paper deep sleep - coded by burst width |
| `sleep_min/sleep_min.ino` | the least possible: 2 s awake, 15 s deep sleep, no Serial, no pins. The baseline (N1) |
| `analyze.py` | turns a `tools/scope_log.py` CSV into a sleep current, a cap leakage, or a wake/sleep cycle breakdown |
| `data/<YYYY-MM-DD>/` | raw scope CSVs, one per test, named after the test ID |

## How the current is measured

```
 LAB SUPPLY +  ──────────────────────────────►  the load's + (XIAO 5V pin, or see part 2)

 LAB SUPPLY −  ──┬───[ 10 Ω ]────┬────────────  the load's GND
                 │               │
                 └──|◄─ 1N5817 ──┘   grey band (cathode) toward supply −
                 │               │
         CH1 ground clip     CH1 probe tip
```

* **Low-side shunt**, so one probe reads it straight against ground. 1 uA is
  0.01 mV across 10 ohm.
* **The 1N5817** carries the 20-30 mA wake bursts, which would otherwise drop
  2-3 V across the shunt and brown the board out. At sleep currents the shunt
  drops well under 1 mV, where the diode passes nothing worth counting.
* **One ground point.** The probe's ground clip goes to supply −, and nothing
  else connects the circuit to the outside world. **No USB cable while
  measuring.** On 2026-09-03 a ground loop through a logger added a fictional
  1.6 mA to every reading.
* **Shunt:** measured **10.1 ohm** - 11.2 ohm on the DMM, minus 1.1 ohm with the
  leads touching. Always subtract the leads.

**Scope (DS1054Z, LAN `192.168.94.12`):** CH1, probe and channel both on **1x**,
DC coupling, **20 MHz bandwidth limit**, acquisition **average**, **1 mV/div**,
timebase 10-50 ms/div. Wake bursts clip off the top of the screen. That is fine,
because those samples are thrown away.

**The zero is the measurement.** With a wire across the shunt, so it carries
nothing, CH1 still reads **0.31-0.35 mV at 1 mV/div** (0.58 mV at 5 mV/div). On
10 ohm that is ~33 uA, more than the bare board draws. So every test goes:
**wire on -> zero -> wire off -> measure -> wire on -> zero**, at the same V/div.
The zero changes with the V/div setting, so it must be taken at the setting you
measure at. Over a whole session it stayed within 0.315-0.351 mV, about +-2 uA.

```
python tools/scope_log.py --ip 192.168.94.12 --channel 1 --interval 1 --duration 600 --out sleep_current/data/<date>/<TEST>.csv
python sleep_current/analyze.py sleep sleep_current/data/<date>/<TEST>.csv --ohms 10.1 --offset-mv <mean of the two zeros>
```

`analyze.py` drops any sample above 3x the median as a wake, and reports the
median of the rest.

## Part 1 - the bare XIAO on the breadboard

A second XIAO ESP32-C3, charge LED removed, nothing on any GPIO, running
`sleep_probe`. Lab supply into its `5V` pin. Flash it over USB *before* wiring it
to the supply. After that it is asleep 98% of the time, so re-flashing needs the
BOOT dance: hold BOOT, tap RESET, release BOOT. Then tap RESET with USB attached,
and it prints its settings once.

| ID | Setup | Expect | Why | Result / conclusion |
| -- | ----- | ------ | --- | ------------------- |
| **Z** | wire across the shunt, everything else as in the test | ~0 mV | the probe's zero | **Done, before and after every test.** 0.31-0.35 mV at 1 mV/div. Not small: it is larger than the signal |
| **R1, R1d, R2, R2d** | resistor loads with and without the diode | 400 uA, 45 uA | check shunt, diode and scope | **Not needed.** The in-place zero covers the scope's offset, the DMM covers the shunt, and at <1 mV across it the diode passes nothing worth counting |
| **A0** | `5V` pin, **5.0 V** | ~400 uA? | is the bare board the 400 uA? | **25 uA (+-2).** Sleep 0.591 mV, zeros 0.351 / 0.325 mV. `A_5V0.csv`, `Z_1mV_after_A_5V0.csv`. **The bare board is not the 400 uA** |
| **A1** | `5V` pin, 4.5 V | | top of the cap range | **Skipped:** A0 at 5.0 V and A2 at 4.0 V bracket it |
| **A2** | `5V` pin, **4.0 V** | | middle of the cap range | **11 uA, later 20 uA.** First run: ~56 s at **~165 uA** right after the 5.0 -> 4.0 V change, then 0.43 mV = 11 uA (`A2_4V0.csv`). Ten minutes later: 19 -> 23 uA, creeping up, 10 normal wakes, no 165 uA stretch (`A2_4V0_10min.csv`). The zeros (0.315 / 0.336 mV) did not move enough to explain the difference, so the board's sleep current itself wanders between ~10 and ~25 uA. The 165 uA stretch is unexplained |
| **A3** | `5V` pin, 3.5 V | | the hold point | **Skipped:** A0 and A2 show no voltage effect bigger than the board's own wander |
| **B1** | `3V3` pin, 3.4 V, `5V` open | 40-50 uA? | what the HT7533 could buy | **Skipped:** A0 is already 25 uA through the onboard regulator, so there is nothing for the HT7533 to win |
| **C1** | as A2, `PARK_PINS 0` | | does pin parking matter? | **Not run.** On a bare board the parked pins have nothing attached; it matters on the sculpture, see S3 |

**Conclusion:** the XIAO alone sleeps at ~10-25 uA through its own `5V` path.
The sculpture's ~400 uA is not the board.

## Part 1b - the D8 pull-up on the bare XIAO, 2026-10-11

**Question:** does the sculpture's 10k from 3V3 to D8 (GPIO8, strapping pin)
cost sleep current? Theory: I = (3V3 - V_D8) / 10k, so zero while the firmware
leaves D8 high-Z, ~330 uA only if D8 is pulled low.

**Setup:** the part 1 breadboard XIAO, flashed with `sleep_probe` **v3** (never
touches GPIO8 except clearing old holds). Lab supply **4.5 V** on `5V`, 10.1 ohm
low-side shunt, **no 1N5817**, no USB. Scope as left from part 2: CH2 on the
shunt, **5 mV/div, offset -15 mV**, 1x; CH1 moved to **D8**, 1 V/div, offset
-2 V, probe 10x. All ground clips on supply -.

| ID | Setup | Result / conclusion |
| -- | ----- | ------------------- |
| **D1** | 10k 3V3 -> D8, first try | Sleeps alternate ~5.9 mV and ~33 mV on CH2. D8 read **2.2 V** on the scope throughout - the resistor was making **poor breadboard contact**. Discard. `D1_d8_pullup.csv`, `D1_d8_volts_zero.csv` (D8 voltage with the zero wire on) |
| **Z** | wire across the shunt | **CH2 1.08 mV** (0.74-2.14), CH1 0.35 mV at that point. The zero for every CH2 reading at 5 mV/div / -15 mV. `Z_after_D1.csv` |
| **DMM** | static checks | R = 9.97 kohm. 3V3 pin 3.28 V. D8 without R: 0.07 V (high-Z input, floats low). D8 with R reseated: **3.25 V**, with or without the scope probe on it. Scope then agreed: 3.29 V |
| **D1b** | 10k reseated, 3 min | **D8 a steady 3.30 V** through sleeps and wakes, so **the pull-up draws ~0-3 uA - ruled out, as the theory says.** But the board's sleep: **~7.0 mV = ~0.59 mA** in four sleeps, **~31.4 mV = ~3.0 mA** in one (the one after the longest wake, 32.5 s wake-to-wake, consistent with phase 2 - not proven: the scope's averaging smears wake widths by ~1 s, and phase 2's second sleep was not high). `D1b_d8_pullup.csv` |

**Conclusions:**

* **The D8 pull-up costs nothing** while D8 is high-Z. Confirmed on the bench.
  Breadboard contacts can lie: check the pin voltage, not just the part.
* **This bare XIAO now sleeps at ~0.59 mA, not part 1's ~25 uA**, and
  **switches between sleep levels** (~0.59 / ~3.0 mA). What differs from part 1:
  `sleep_probe` v3 instead of v1, 4.5 V instead of 4.0/5.0 V (part 1 found no
  voltage effect), no 1N5817.
* So the sculpture's high, two-level sleep is **not explained by its modules**:
  a bare XIAO does it too. Either v3 does it, or the C3 sometimes misses its
  proper deep-sleep state.

## Part 1c - checking the setup, and a clean repeat, 2026-10-16

**Question:** is the bare board's ~0.59 / ~3.0 mA real, or the measurement?

**Setup:** as part 1b - breadboard XIAO, `sleep_probe` v3, 10k 3V3 -> D8 still
fitted, 4.5 V on `5V`, 10.1 ohm shunt, no 1N5817, no USB, only the lab supply.
CH2 on the shunt, 1x, 20 MHz limit, average 4.

**The jumper zero is not a zero on a breadboard.** With a jumper wire across the
shunt, CH2 read 2.1 mV at 5 mV/div (drifting 2.3 -> 1.3 mV in a minute), 3.1 mV
at 2 mV/div, 2.1 mV at 1 mV/div - against 0.33 mV on 10-03 at the same setting.
With the **probe tip clipped to its own ground clip** on supply -, CH2 read a
steady **0.43 mV (+-0.03) at both 1 mV/div and 5 mV/div**. So the scope is fine
and the extra ~1.7 mV came from the breadboard: the jumper and its contacts
still carry the board's current, and their few ohms wander. From now on:

* **zero = probe tip on its own ground clip**, not a jumper across the shunt
* **clip CH2's tip and ground directly onto the shunt resistor's leads**, so no
  breadboard contact is in the measured path

`Z1`-`Z3` are the jumper zeros, `Z4`/`Z5` the tip-on-ground zeros, `C0` the
board found unpowered (CH2 2.5-3.1 mV with no current - the floating input).

**M1 - clips on the shunt's leads, 3 min.** Current = (CH2 - 0.43 mV) / 10.1 ohm.
The wakes are smeared over 1-4 s by the averaging, so their widths cannot be
read; the sleeps between them are flat.

| Sleep | Time | CH2 | Current |
| ----- | ---- | --- | ------- |
| 1 | 0-24 s | 33.0 mV | **3.2 mA** |
| 2 | 26-57 s | 5.97 mV | **0.549 mA** |
| 3 | 62-89 s | 5.98 mV | 0.549 mA |
| 4 | 92-121 s | 32.9 mV | **3.2 mA** |
| 5 | 124-150 s | 5.97 mV | 0.549 mA |
| 6 | 154-180 s | 5.98 mV | 0.549 mA |

`M1_clips_on_shunt.csv`.

**Conclusions:**

* **It reproduces.** Low level **549 uA**, the same to 0.01 mV over four sleeps;
  high level **3.2 mA**, **every third sleep**. Part 1b's 0.59 / 3.0 mA were
  close; their jumper zero (1.08 mV) was the error.
* **Every third sleep does not fit v3's phases** (p0 p0 p1 p1 p2 p2), so it is
  not simply "phase 2 holds the e-paper pins".
* 3.2 mA is neither awake (~20 mA) nor deep sleep (~5-25 uA). Part 1's 25 uA on
  this same board is still 20x below the low level, and unexplained.
* Not yet cross-checked with a second instrument (DMM in series, mA range).

**M2 - 10k off D8, CH3 on D8, CH1 on D3, 4 min.** D3 (GPIO5, EPD_RST) is held
high through sleep only in v3's phase 2, so it tags the phase. Sleeps in order:

| Sleep | Time | D3 asleep | Phase | Current |
| ----- | ---- | --------- | ----- | ------- |
| 0 | 0-11 s (tail) | low | 1 | **3.2 mA** |
| 1 | 17-44 s | high | 2 | **3.2 mA** |
| 2 | 52-76 s | high | 2 | 0.53 mA |
| 3 | 82-107 s | low | 0 | 0.53 mA |
| 4 | 113-137 s | low | 0 | 0.53 mA |
| 5 | 145-168 s | low | 1 | 0.53 mA |
| 6 | 178-200 s | low | 1 | **3.2 mA** |
| 7 | 209-232 s | high | 2 | 0.53 mA |
| 8 | 237- s (head) | high | 2 | **3.2 mA** |

`M2_no_pullup_d3_d8.csv`. Phases inferred from D3 and v3's order p0 p0 p1 p1
p2 p2.

* **D8 sits at ~0 V through every sleep and wake**, high or low current - it
  plays no part. The low level is 0.53 mA (0.549 mA in M1 with the 10k; the
  ~20 uA difference is about one ADC step at 5 mV/div).
* **The high sleeps are not tied to a phase**: they come in phase 1 (D3 low)
  and in phase 2 (D3 high), and the same phase is high one round and low the
  next. The pattern is irregular - here H H L L L L H L H, in M1 H L L H L L -
  about 4 sleeps in 10. So it is **not v3's pin holds**. Whether a sleep comes
  out at 0.53 or 3.2 mA is decided some other way, per sleep.
* The current changes over several seconds around each wake (e.g. 5.8 -> 33 mV
  over ~9 s at 169-178 s), longer than the averaging smear.

**M3 - as M2, 10 min, 20 sleeps.** M2 had no high sleep in phase 0 (2 seen), and
phase 0 is the only wake without I2C (`bmeSleep()` on a bus with no BME280) -
v1, the 25 uA firmware, never used I2C either. This run tests that.

| Sleep | Phase | Current | | Sleep | Phase | Current |
| ----- | ----- | ------- | - | ----- | ----- | ------- |
| 0 | 0 | 0.52 mA | | 10 | 2 | **3.20 mA** |
| 1 | 1 | **3.20 mA** | | 11 | 0 | 0.52 mA |
| 2 | 1 | **3.20 mA** | | 12 | 0 | **3.26 mA** |
| 3 | 2 | 0.52 mA | | 13 | 1 | 0.52 mA |
| 4 | 2 | **3.26 mA** | | 14 | 1 | **3.20 mA** |
| 5 | 0 | **3.20 mA** | | 15 | 2 | **3.26 mA** |
| 6 | 0 | 0.52 mA | | 16 | 2 | 0.52 mA |
| 7 | 1 | 0.52 mA | | 17a | 0 | **3.26 mA** |
| 8 | 1 | 0.52 mA | | 17b | 0 | **3.20 mA** |
| 9 | 2 | 0.52 mA | | 18 | 1 | 0.52 mA |

`M3_10min_phases.csv`. Phase from D3 (high = phase 2, the two sleeps before a
D3 pair are phase 1). The wake between 17a and 17b shows on CH2 as a bump of
only 0.7 mV - the averaged VAVG can all but miss a 0.5 s phase 0 burst - but
the level steps 33.4 -> 32.8 mV across it.

| Phase | Wake does | Low | High |
| ----- | --------- | --- | ---- |
| 0 | nothing | 3 | **4** |
| 1 | I2C | 4 | 3 |
| 2 | I2C + e-paper pins | 3 | 3 |

* **No relation to the phase.** Phase 0, with no I2C, is high as often as the
  others. The I2C idea is dead, and so is any firmware-phase explanation.
* **Half the sleeps are high** (10 of 20), in runs: L H H L H H L L L L H L H L
  H H L H H L. Highs often follow highs across a wake (1-2, 4-5, 14-15,
  17a-17b), so the state can survive a wake.
* **Two high sub-levels**, 32.8 and 33.3-33.4 mV (3.20 / 3.26 mA); the low level
  is one, 5.67-5.73 mV (0.52 mA).
* D8 stayed at ~0 V throughout (CH3 -0.012 to -0.008 V).

**B1 - lab supply 3.30 V straight into `3V3`, `5V` open, 10 min, 19 sleeps.**
Bypasses the charger (ETA4054, no battery) and the onboard regulator. Shunt,
clips and probes as M3. `B1_3V3_direct.csv`.

| Phase | Low | High |
| ----- | --- | ---- |
| 0 | 3 | 4 |
| 1 | 2 | 3 |
| 2 | 4 | 3 |

Order: L H H L H H L L H H H L H H L L L H H (sleep 0 is phase 0).

* **The two levels survive with the 5 V side cut out**: low **0.40-0.42 mA**,
  high **2.93-3.11 mA**, half the sleeps high, no relation to the phase. So it
  is **not the charger or the regulator** - it is on the 3.3 V side: the C3, its
  flash, or something else on that rail.
* The low level fell from 0.52 to 0.41 mA: ~110 uA was the 5 V side (or the
  regulator's reverse leakage now that its output is driven - not separable).
* **After every wake the current decays over ~8-10 s** to the sleep level (e.g.
  25 -> 4.6 mV over 89-101 s), and rises into a high sleep over ~6-10 s. D3,
  through the same averaging, steps within one sample, so this is **not scope
  smear** - the board really settles over seconds.

**N1 - `sleep_min` on 3.3 V into `3V3`, 5 min, 17 wakes.** New sketch
`sleep_min/sleep_min.ino`: `delay(2000)`, 15 s timer deep sleep, nothing else -
no Serial (built `CDCOnBoot=cdc`, which is *disabled* on this board), no pins,
no holds. Flashed by esptool (four images), then power-cycled from the lab
supply. `N1_sleep_min_3V3.csv`.

* **Every sleep is low: 32-94 uA**, read as the median of the last 2 s before
  each wake. **No 0.4 mA level, no 3 mA level**, 0 of 17. Wake period 17.00 s,
  as programmed.
* That is Seeed's 44 uA board figure and part 1's 25 uA. **The board is fine;
  the 0.4 / 3 mA sleeps come from something v3 does.**
* The ~10 s decay after each wake is still there with this sketch, so it is not
  part of the two-level problem. It may be CH2 recovering from overload (the
  ~200 mV wake against 5 mV/div) - D3's clean steps were at 1 V/div, so they
  do not rule that out. The floors above are still on that slope, so the true
  floor is at or below them; at 5 mV/div one ADC step is ~20 uA.
* Left between `sleep_min` and v3: **USB CDC enabled**, and **v3's pin parking
  with holds** (`INPUT_PULLUP` + hold on GPIO20/2, `INPUT_PULLDOWN` + hold on
  GPIO10, `gpio_deep_sleep_hold_en()`). v1 had both and measured 25 uA in part
  1, so it may take the two together. Bisect: A = `sleep_min` built with
  `CDCOnBoot=default`; B = `sleep_min` + phase 0's parking.

**N2 - bisect A: `sleep_min` built with `CDCOnBoot=default` (CDC enabled), 3.3 V
into `3V3`, 5 min, 18 wakes.** `N2_sleep_min_cdc_3V3.csv`. Period 17.02 s. The
highest sample in the run is 19.9 mV, inside a wake; **no sleep near 3 mA or
held at 0.4 mA**. Sleeps settle to ~0.6 mV, ~15-20 uA. **USB CDC is not the
cause.** Left: v3's pin parking and holds (bisect B).

**N3 - bisect B, 2026-10-23: `sleep_min -DPARK=1`, `CDCOnBoot=cdc`, 3.3 V into
`3V3`, 5 min, 18 sleeps.** `../2026-10-23/N3_sleep_min_park_3V3.csv`. Only v3's
phase 0 `parkPins()` added: `INPUT_PULLUP` + `gpio_hold_en` on GPIO20 and GPIO2,
`INPUT_PULLDOWN` + hold on GPIO10, `gpio_deep_sleep_hold_en()`.

* **Both v3 levels are back**: **15 of 18 sleeps high, 2.8-3.4 mA**; 3 low at
  **0.51 mA** (5.6 mV). Without the holds (N1, N2) the same sketch sleeps at
  ~20 uA. **The pin holds cause it** - on pins with nothing connected.
* The high level comes in several repeating steps: 34.7, 32.5, 31.3, 30.3,
  28.5 mV - as if several independent contributions switch on and off.
  Untested idea: a held pad keeps its input buffer on but not its pull, and
  floats - which would also fit the ~10 s ramps.
* Wakes from a high sleep are only small bumps on CH2, so read sleeps from the
  trace by the 17 s period, not by a wake detector.
* Still unexplained: v1 had the same three holds and measured 25 uA in part 1.

Next: `-DPARK=1 -DPARK_MASK=<n>` holds one pin at a time (1 = GPIO20,
2 = GPIO2, 4 = GPIO10); `-DPARK_GLOBAL=0` drops `gpio_deep_sleep_hold_en()`.

**Online, 2026-10-23:** others report the same. On a bare C3 at 3.3 V:
`esp_deep_sleep_start()` alone 7 uA, adding `gpio_deep_sleep_hold_en()` 98 uA,
plus `INPUT_PULLUP` on two pins 3 mA; dropping the hold call brought it under
10 uA (forum.arduino.cc/t/esp32-c3-deep-sleep-weird-power-draw-use-cases/1413974).
ESP-IDF #12183 (C3): `esp_deep_sleep_start()` sets the pads' pulls itself, and
holds override it. Nothing official explains the mA. The bisect builds (one
pin at a time) were skipped in favour of testing the node firmware directly.

**N4 - node firmware v1.5, `PARK_PINS 0`, built `-DCYCLE_S=15 -DHOLD_S=15`,
bare board, 3.3 V into `3V3`, 5 min, 20 wakes.**
`../2026-10-23/N4_node_v15_nopark_3V3.csv`. Wakes every ~15.2 s, each a small
bump (~1.7 mV): with VSENSE floating the node read below `v_floor` and took the
**hold path** every time (read Vcap, sleep). **Sleeps ~0.9 mV = ~45 uA, no high
sleeps, highest sample 1.8 mV.** Without parking, the node's sleep path is
clean. Not yet tested: the full-cycle path (I2C + e-paper SPI before sleep) -
tie GPIO3 (D1) to `3V3` so VSENSE reads ~6.6 V and every wake is a full cycle.

**N5 - as N4, GPIO3 (VSENSE) tied to 3.3 V, 5 min, 12 cycles.**
`../2026-10-23/N5_node_v15_nopark_fullcycle.csv`. Every wake is now a full cycle
(I2C scan, e-paper refresh into nothing), ~9 s awake (CH2 pinned at the top of
the screen, ~40 mV - this run does not measure the awake current), then 15 s
asleep, period ~24 s. **Every sleep settles to ~0.9-1.2 mV = ~45-80 uA, no
0.5 / 3 mA sleeps.** Both of the node's sleep paths are clean without parking.

**Conclusion of part 1c:** the 0.5 / 3 mA sleeps on the bare XIAO are caused by
`parkPins()` - pulls latched with `gpio_hold_en` + `gpio_deep_sleep_hold_en()`
before deep sleep. Node firmware v1.5 has `PARK_PINS 0`. Not yet shown: that
this also fixes the sculpture's ~0.86 mA / 340 uA, where the BME280 breakout's
pull-ups and the e-paper are attached.

## Part 2 - the integrated sculpture

Everything on the sculpture is soldered, including the e-paper, so the one
break point is the **VCAP jumper**: a 2.54 mm header with a removable jumper cap
(`photo/jumper.jpg`), between the cap and the XIAO's `5V` pin. With the cap
pulled, the panels and the cap are cut off from the XIAO, and the supply can be
fed onto either header pin with a female dupont lead.

Only VSENSE still crosses: the divider sits on the cap side, so **the firmware
keeps reading the cap, not the supply.** That is useful. The cap's voltage
decides what the firmware does, and the supply's voltage decides nothing.

### Setup: one shunt, two channels, no diode

```
                         jumper cap REMOVED
   cap + / divider  ──o              o──  XIAO 5V
   (pin V, cap side)                      (pin X, XIAO side)

 LAB SUPPLY +  ── female dupont ──►  pin V (step 1)  or  pin X (step 2)

 LAB SUPPLY −  ──┬────[ 10.1 Ω ]────┬────────  sculpture GND
                 │                  │          (clip on a bare GND wire bend,
     CH1 + CH2 ground clips         │           or the cap's − leg)
                                    │
                         CH1 tip and CH2 tip, both here
```

* **No diode this time.** The point is to measure the wake as well, and the
  diode would carry the wake current around the shunt. A ~30 mA wake drops
  ~0.3 V across 10 ohm, so run the supply at **4.5 V**, and the XIAO's `5V` pin
  still sees ~4.2 V. The onboard regulator makes 3.3 V either way, so the supply
  voltage does not change what the board draws.
* **Both probes on the same point, at different scales.**
  * CH1, coarse, for the wake: **100 mV/div, offset -300 mV**, so 0-80 mA is on
    screen.
  * CH2, fine, for the sleep: **2 mV/div, offset -6 mV**, so 0-1.4 mA is on
    screen.

  During a wake CH2 clips and CH1 does the work; asleep, CH1 reads ~nothing and
  CH2 does. One log captures both, and `analyze.py cycle` splits them. Both
  probes and both channels on **1x**, 20 MHz bandwidth limit, acquisition
  average, timebase 50 ms/div.
* **One ground point.** Both probe clips and supply − on the shunt's supply
  side. Supply − reaches the sculpture *only* through the shunt: no USB, nothing
  else touching it. The cap and panels hang off sculpture GND, but their only
  path back to the supply is through the shunt, so they add nothing.
* **Zero:** a wire from supply − to sculpture GND, across the shunt. That zeros
  both channels at their own scales. Take it before and after every step, as in
  part 1.
* **Panels covered** with a box or cloth. They still charge the cap, and in
  step 1 that would hide the leakage.
* Supply current limit: **150 mA**.

### Step 1 - charge the cap, and measure its leakage on the way

Supply **4.00 V** onto **pin V**. The XIAO is unpowered. The supply charges the
cap through the shunt: up to 4 F x 1.7 V = 6.8 C if it is as flat as it was
found, about a minute at the current limit. Then the current decays towards the
cap's own leakage. Log it for **1-2 h**:

```
python tools/scope_log.py --ip 192.168.94.12 --channel 1,2 --interval 5 --out sleep_current/data/<date>/S1_cap_4V0.csv
```

The floor it settles to is **the real cap's leakage at 4.0 V, in place**, plus
the divider's ~2 uA. If it is still falling after 2 h, take the last value as an
upper bound and move on.

Leaving the cap at 4.0 V also sets up step 2: it is above the 3.50 V hold
threshold, so the firmware will cycle normally.

### Step 2 - the sculpture, wake and sleep

Zero first (wire on, then off). Move the dupont lead from **pin V to pin X**,
supply **4.50 V**. That is a cold boot. The firmware reads the cap at ~4.0 V and
runs a full cycle: BME280, panel refresh, hibernate, sleep 300 s, and repeat. The
cap stays at ~4.0 V, because nothing loads it now except the divider and its own
leakage. The panel shows a fresh frame every 300 s, which confirms it is cycling.

Log **20 minutes** (4 wakes) at 4 samples a second:

```
python tools/scope_log.py --ip 192.168.94.12 --channel 1,2 --interval 0.25 --duration 1200 --out sleep_current/data/<date>/S2_cycle.csv
python sleep_current/analyze.py cycle sleep_current/data/<date>/S2_cycle.csv --ohms 10.1 --wake-offset-mv <CH1 zero> --offset-mv <CH2 zero>
```

`analyze.py cycle` prints:

* for each wake: duration, mean and peak current, and charge, in mC and as
  millivolts on 4 F
* the median sleep current between wakes, in uA and as mV per 300 s and per
  900 s
* the whole cycle's average current

Those millivolts are the flash log's units, so they compare directly with the
2026-10-03 run: ~65 mV per update, ~90 mV per 900 s of held sleep.

| ID | Setup | Expect | Why | Result / conclusion |
| -- | ----- | ------ | --- | ------------------- |
| **S1** | pin V, 4.00 V, XIAO off, panels covered, 1-2 h | charging current decaying to a floor | the real cap's leakage, at night voltage, in place | |
| **S2** | pin X, 4.50 V, cap at ~4.0 V from S1, 20 min | ~260 mC per wake; sleep somewhere between 25 and 400 uA | the sculpture's wake cost and sleep current, directly | **First look, 2026-10-03 - not yet zeroed:** sleep **~0.87-0.93 mA** (CH2 9.15-9.74 mV, earlier no-current reading 0.34 mV). Wake ~15 s at **~21 mA** - CH1's probe turned out to be on 10x, so its readings are x10. Cap at 3.83 V, so this was normal cycling (`cycle_s` 60), not the hold state. **That is ~40x the bare board and ~2x the night's 400 uA.** Charge LED confirmed desoldered on the sculpture, so it is the BME280 breakout or the e-paper module. S1 skipped. `S2_first_look3.csv`, `S2_4V5.csv` (80 s, interrupted). `S2_first_look.csv` / `2` were taken with CH2 unconnected - ignore their CH2 |
| **S3** | as S2, firmware holding the e-paper lines through sleep | | are the panel's floating inputs the extra sleep current? | **2026-10-04, v1.4 `park_epd 1`: no effect - sleep ~1.0 mA** (CH2 median 10.59 mV; 9.95-10.65 between wakes), against S2's ~0.9 mA. Wakes every 69 s (60 s + ~9 s awake), panel refreshing. **The floating e-paper lines are not the cause.** CH1 peaked at ~29.7 mV in each wake, which on 1x would be only ~3 mA - at odds with the ~29 mA the flash log implies (65 mV x 4 F / 9 s). CH1's probe setting is uncertain; check it, and the supply's own display during a refresh. `S3_park_epd1_4V5.csv` |
| **P1** | sculpture running `sleep_probe` v1: never touches the BME280 or the panel | ~25 uA if the firmware is the cause | split hardware from node firmware | **2026-10-04: ~340 uA** in three sleeps, after a first stretch at **~1.25 mA**. D8 read 3.3 V asleep (its 10k pull-up draws nothing); D3, the e-paper RST, read 0 V. `P_sleep_probe_4V5.csv` |
| **P2** | `sleep_probe` v2: e-paper lines rotated through untouched / RST+CS up / all down | | does a floating or low RST cost current? | **~860 / ~1020 / ~930 uA**, each twice, identical. RST held low costs nothing; pulling it up costs ~160 uA more. **The e-paper lines are worth ~0.1 mA at most.** Baseline back at ~860 uA, not P1's 340 - something switches between two levels. `P2_epd_states_4V5.csv` |
| **P3** | `sleep_probe` v3: BME280 commanded to sleep; then also e-paper reset + deep sleep command, RST held high | a drop to ~25-50 uA in the phase that hits the culprit | find which part is awake | **No phase drops below ~860 uA** (all sleeps 860-964 uA). Either the commands did not take, or the ~0.86 mA is not a chip left awake but something static: a module's own regulator or level shifter, the sculpture's XIAO itself (part 1 measured a *different* XIAO), or a leakage path in the wiring. `P3_cmd_sleep_4V5.csv` |
| **U1** | BME280 **VCC wire only** unsoldered; SDA, SCL, GND still connected. `sleep_probe` v3 | ~25-50 uA if the BME280 was the load | is it the BME280? | **2026-10-04: not lower - ~1.4 mA rising to ~1.8 mA over 2 min** (CH2 15.1 -> 18.9 mV at 5 mV/div, offset -15 mV; no zero taken at that setting yet, so +-0.1 mA). Wakes normal, every ~31.5 s. **Not clean:** with only VCC off, the BME280 is back-powered through SDA/SCL, which the C3 holds high with its pull-ups during sleep - that can draw odd, drifting current. SDA and SCL must come off too, or the whole breakout. `U1_no_bme.csv` (2 mV/div, partly clipped), `U1_no_bme_5mVdiv.csv` |
| **U2** | BME280 VCC soldered back (its GND may have been a poor joint; reflowed). DMM: SDA and SCL read high at the BME280 during sleep | | did the poor GND or the loose VCC cause it? | **2026-10-04: back to ~0.86 mA** (CH2 ~9.17 mV at 5 mV/div offset -15 mV, one sleep in four ~9.95 mV; no zero at this setting yet, +-0.05 mA). **The resoldering changed nothing, and SDA/SCL are high asleep, so the breakout's pull-ups draw nothing.** `U2_bme_resoldered.csv` |
| **V** | DMM, supply on pin X | | does VSENSE or the cap leak into the measurement? | Pin V 3.10 V (not the supply's 4.5 V), pin X 0.0 V with the supply off: **the cap is isolated, no hidden path.** D1 1.22 V against the expected 1.55 V: the asleep XIAO loads the tap slightly, a few uA at most. The cap fell from ~3.83 V to 3.10 V while isolated overnight |

**Reading the results:**

* **S2 sleep vs ~25 uA (part 1):** the difference is the BME280 breakout, the
  e-paper module and the charge LED, if the sculpture's board still has one.
* **S1 + S2 sleep vs ~400 uA:** if they add up, the night is explained. If they
  fall well short, the 400 uA figure itself is suspect: the nominal 4 F, or
  light in the room that night.
* **S2 wake vs ~65 mV per update:** a check on the day budget. The log measured
  the update cost only as a voltage step on a cap of nominal value.
* **If S2's sleep is high**, S3 tests the likeliest cause, and it needs only
  firmware. In deep sleep the C3 lets go of the e-paper's RST, CS, DC, SCK and
  MOSI. Floating inputs on the panel's controller can draw current, and a
  floating RST can even pulse the panel out of hibernate. Holding those lines at
  their idle levels, as `parkPins()` already does for I2C, costs nothing in
  hardware.

### Optional: the night state

S2 measures the sleep between 300 s cycles. The night is the same hardware state
with a different frame: the panel hibernated, then 900 s polls of ~0.3 s.
Matching it exactly needs the cap between 3.25 and 3.50 V, so the firmware holds.

The supply cannot pull the cap down, but **100 ohm from pin V to GND for ~2 min**
takes it from 4.0 to ~3.4 V (time constant 400 s; watch it with the DMM). Then
reconnect the supply on pin X. The cold boot runs one cycle, and the next wake
draws the hold frame and holds. Expect the same sleep current as S2. Run this
only if S2's sleep looks odd.

## Candidates for the ~0.86 mA - overview, 2026-10-11

**What the results so far already say:**

* **Hardware, not firmware.** Bare XIAO 10-25 uA; sculpture ~0.86-1.0 mA at
  4.5 V with the node firmware *and* with `sleep_probe`.
* **It switches between levels:** ~340 uA once (P1), ~860 uA usually, ~1.25 mA
  once. A fixed resistive leak does not do that - something changes state, or a
  contact is intermittent.
* **0.86 mA at 3.3 V is ~3.8 kohm.** Far too low for flux residue or the 1M
  divider: a real current path or an active circuit.
* **Bench > night.** ~0.86 mA at 4.5 V on the bench, but the night's ~400 uA at
  3.4-2.8 V *includes* the cap's own leakage. The draw may depend on supply
  voltage. Anything on the 3V3 rail would not (the regulator holds it constant
  down to ~3.4 V), so **this hints at the 5V/VCAP side.** Not yet tested - it was
  step 2 of the 10-03 handoff.
* **Ruled out by firmware:** commanding BME280 and e-paper to sleep, and holding
  the e-paper lines (holds are set correctly, including
  `gpio_deep_sleep_hold_en()`). The e-paper RST moves the current by ~160 uA, so
  the module reacts to its lines - but that is not the baseline.

**Suspects, roughly most likely first:**

| # | Suspect | For | Against |
| - | ------- | --- | ------- |
| 1 | **XIAO charge IC / 5V side** - no battery fitted, charger input on the `5V` pin | Fits the voltage dependence (a battery-less charger may detect and retry above its turn-on voltage) and the level switching. Also: a wire bend touching the B+/B- pads underneath | The bare XIAO at 5.0 V was fine - but that was a different board |
| 2 | **The sculpture's XIAO itself** - a GPIO's protection diodes damaged by soldering heat or ESD, or a different board revision | Part 1 never measured this board. A damaged pad can leak mA | No direct evidence |
| 3 | **E-paper module circuitry** - booster MOSFET half-on if its gate (GDR) floats (VCC -> inductor -> MOSFET -> sense resistor -> GND), an LDO or level shifter on the driver board, or damage from the BUSY pad break | A half-on MOSFET drifts and toggles. The module does react to RST. The BUSY break is a known injury | The deep sleep command did nothing - though it may not have taken |
| 4 | **BME280 breakout's LDO / BSS138 level shifters** (purple 4-pin boards usually have both) | U1 shows it can draw odd, drifting current when back-fed | The sleep command did nothing; SDA/SCL high, so the pull-ups draw nothing |
| 5 | **Leak or near-short in the wire bends** - two bends almost touching, a whisker, the leftover GPIO10 wire to the broken BUSY pad | An intermittent contact explains the level jumps, and could follow temperature or flexing | 3.8 kohm is fairly solid for a near-short |
| 6 | **Something on the 5V/VCAP net that should be on 3V3** - e.g. a module's VIN on pin X | Explains the voltage dependence | The pin map says 3V3 - but not checked with a meter |

**Cheapest tests first - the first three need no soldering:**

1. **Supply voltage sweep:** 5.0 / 4.5 / 4.0 / 3.6 V on pin X, 2 min each.
   Current tracks the voltage -> 5V side (#1, #6). Flat -> 3V3 side (#2-#5).
2. **Hold the XIAO's RESET button** (EN low) while logging. The C3 is then off
   (~1 uA). If ~0.86 mA remains, it is the modules or the wiring, not the C3. The
   module lines float meanwhile, so it is a rough split.
3. **Bench 3.3 V into `3V3`, `5V` open.** Charger and onboard regulator drop out.
   If the current falls to ~25-50 uA, it is #1.
4. While logging, **flex and poke the wire bends** with a plastic tool and watch
   CH2 for jumps (#5). Read the regulator / level shifter markings on both modules
   with a loupe and look up their quiescent currents (#3, #4). Ohm out pin X to
   3V3 and pin X to GND with everything off (#6).
5. Only then the soldering splits below: **e-paper VCC off**, then **BME280 VCC
   and GND off together**.

## Where this left off - 2026-10-23

**Found: `parkPins()` causes the 0.5 / 3 mA sleeps.** On the bare XIAO, a bare
sleep sketch draws ~20 uA (N1, N2); adding the node's `parkPins()` gives 0.51 /
~3 mA at random (N3); node firmware **v1.5 with `PARK_PINS 0`** sleeps at
~45-80 uA on both its hold path (N4) and full-cycle path (N5). See *Part 1c*.

**Bench state, as left:**

* **breadboard XIAO** (MAC 94:a9:90:6b:62:7c) running **node firmware v1.5**,
  test build `-DCYCLE_S=15 -DHOLD_S=15` - not the 300 s default
* **lab supply 3.30 V straight into `3V3`**, `5V` open. Never more than 3.3 V on
  that pin. Disconnect it before plugging in USB
* **GPIO3 (D1, VSENSE) jumpered to 3.3 V** so every wake is a full cycle
* 10.1 ohm low-side shunt, no 1N5817, no zero wire, 10k off D8, no USB
* scope **192.168.94.72** (DHCP moves it; `tools/scope_log.py --list`):
  CH1 on D3 (1 V/div, -2 V, probe 10x); **CH2 clipped on the shunt's leads**
  (5 mV/div, -15 mV, 1x, 20 MHz, average 4); CH3 on D8 (1 V/div, -2 V, 1x)
* zero = CH2 tip on its own ground clip = 0.43 mV (2026-10-16)
* the sculpture is untouched since 2026-10-04: still on `sleep_probe` v3

**Next:**

1. **The sculpture:** flash node v1.5 (300 s default, or a `-DCYCLE_S=60` test
   build) and measure its sleep the part 2 way (lab supply on pin X, shunt, cap
   off at the jumper). Power-cycle after flashing - v1.4's holds survive a
   reflash (v1.5's `unparkPins()` also releases them on every boot now). If the
   ~0.86 mA falls to tens of uA, the night problem is solved in firmware.
2. Then set the sculpture back to normal running: `node_cfg.py cycle_s=300`,
   jumper cap on, USB off, and charge the cap first (it was at 3.10 V).
3. Open, lower priority: why v1 (same holds) measured 25 uA in part 1; the
   one-pin-at-a-time bisect (`sleep_min -DPARK=1 -DPARK_MASK=1/2/4`,
   `-DPARK_GLOBAL=0`) would say which hold does it.

**Flashing (no arduino-cli upload):** supply off, USB in, BOOT dance, then

```
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C3:CDCOnBoot=default --build-property "compiler.cpp.extra_flags=-DCYCLE_S=15 -DHOLD_S=15" --output-dir <out> proto_epaper_esp32c3
esptool --chip esp32c3 --port COM7 --baud 460800 --before no-reset --after hard-reset write-flash -z --flash-mode keep --flash-freq keep --flash-size keep 0x0 <out>/<sketch>.ino.bootloader.bin 0x8000 <out>/<sketch>.ino.partitions.bin 0xe000 <core>/tools/partitions/boot_app0.bin 0x10000 <out>/<sketch>.ino.bin
```

(`sleep_min` is built with `CDCOnBoot=cdc`, i.e. CDC disabled on the XIAO.)
`compile` exits 1 on a harmless post-build hook. Then USB out, supply on.

**Measuring:** `python tools/scope_log.py --ip 192.168.94.72 --channel 1,2,3
--interval 0.25 --duration 300 --out sleep_current/data/<date>/<ID>.csv`.
Current = (CH2 - 0.43 mV) / 10.1 ohm. Read a sleep from its last 2 s - CH2
decays for ~10 s after every wake.

### Previous handoff - 2026-10-16

**The finding of the day: the board is fine, `sleep_probe` v3 causes the 0.4 /
3 mA sleeps.** A bare `sleep_min` sleeps at ~15-90 uA with no high sleeps at all
(N1), and enabling USB CDC does not change that (N2). See *Part 1c*.

| Build | CDC | v3 pin parking + holds | High sleeps |
| ----- | --- | ---------------------- | ----------- |
| v3, 3.3 V into `3V3` (B1) | on | yes | 10 of 19 |
| `sleep_min` (N1) | off | no | 0 of 17 |
| `sleep_min` + CDC (N2) | on | no | 0 of 18 |
| **`sleep_min` -DPARK=1 (B)** | off | yes | **next** |

**Bench state, as left:**

* **breadboard XIAO** (MAC 94:a9:90:6b:62:7c), running **`sleep_min` built with
  `CDCOnBoot=default`** (variant A, the N2 build). 2 s awake, 15 s asleep
* **lab supply 3.30 V straight into `3V3`, `5V` open** (bypasses charger and
  regulator). Never put more than 3.3 V on that pin. Disconnect it before USB
* 10.1 ohm low-side shunt, **no 1N5817, no zero wire**, **10k off D8**, no USB
* scope **192.168.94.72** (moved from .12 by 2026-10-23; `tools/scope_log.py --list` finds it): **CH1 on D3** (1 V/div, offset -2 V, scope probe 10x);
  **CH2 clipped directly on the shunt resistor's leads** (5 mV/div, offset
  -15 mV, 1x, 20 MHz, average 4); **CH3 on D8** (1 V/div, -2 V, scope probe 1x)
* **zero = CH2 tip on its own ground clip = 0.43 mV.** Not a jumper across the
  shunt - on the breadboard that read 1.1-3.1 mV and wandered (*Part 1c*)
* uncommitted, deliberately: the stray `as` -> `ast` in
  `proto_epaper_esp32c3.ino`'s VDIV_CAL comment - not part of this work

**Next, in order:**

1. **Bisect B:** flash `sleep_min` with `-DPARK=1` and `CDCOnBoot=cdc` - v3's
   phase 0 `parkPins()` verbatim (pull-ups held on GPIO20/GPIO2, pull-down held
   on GPIO10, `gpio_deep_sleep_hold_en()`). Log 5 min, count high sleeps.
   * high sleeps back -> the holds cause it. Then split further: GPIO2 alone
     (strapping pin), GPIO20 alone, without `gpio_deep_sleep_hold_en()`.
   * still none -> it is something else in v3; add v3's pieces one at a time
     (the cold-boot `HWCDC::isPlugged()` loop, `bmeSleep()`, `epdDeepSleep()`,
     phase 2's extra holds, the 30 s sleep).
   * Puzzle to keep in mind: **v1 had CDC on and the same three holds and
     measured 25 uA in part 1** on this same board.
2. Once found, check the node firmware (`proto_epaper_esp32c3`) for the same
   cause - its `parkPins()` is where v1/v3's came from - and re-measure the
   sculpture, whose ~0.86 mA / 340 uA two-level sleep looks like the same thing.

**Flashing (no arduino-cli upload):** supply off `3V3`, USB in, BOOT dance
(hold BOOT, tap RESET, release BOOT), then:

```
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C3:CDCOnBoot=cdc --build-property "compiler.cpp.extra_flags=-DPARK=1" --output-dir <out> sleep_current/sleep_min
esptool --chip esp32c3 --port COM7 --baud 460800 --before no-reset --after hard-reset write-flash -z --flash-mode keep --flash-freq keep --flash-size keep 0x0 <out>/sleep_min.ino.bootloader.bin 0x8000 <out>/sleep_min.ino.partitions.bin 0xe000 <core>/tools/partitions/boot_app0.bin 0x10000 <out>/sleep_min.ino.bin
```

`compile` exits 1 on a harmless post-build `Copy-Item` hook; the binaries are
there. Then USB out, supply on (that power cycle also clears old holds).
`CDCOnBoot=cdc` is CDC *disabled* on the XIAO definition.

**Measuring (5 min, ~17 sleeps):**

```
python tools/scope_log.py --ip 192.168.94.72 --channel 1,2,3 --interval 0.25 --duration 300 --out sleep_current/data/<date>/<ID>.csv
```

Current = (CH2 - 0.43 mV) / 10.1 ohm. Low v3 sleep ~4.5-5.7 mV, high ~30-33 mV;
`sleep_min` sleeps settle to ~0.6-1 mV. After every wake CH2 decays over ~10 s
(perhaps CH2 recovering from the ~200 mV overload), so read a sleep from its
last 2 s.

### Previous handoff - 2026-10-11

**Bench state, as left:**

* **breadboard XIAO** (not the sculpture) running **`sleep_probe` v3**, 10k
  3V3 -> D8 still fitted (proven harmless), 4.5 V on `5V`, 10.1 ohm shunt, no
  diode, no USB, **zero wire off**
* scope 192.168.94.12: **CH1 on D8, 1 V/div, offset -2 V, channel probe set to
  10x** (changed from the sculpture setup); **CH2 on the shunt, 5 mV/div,
  offset -15 mV, 1x, zero 1.08 mV**. Extra probes are available for CH3/CH4
* the sculpture is untouched since 2026-10-04 (see below)
* uncommitted: a stray one-character edit in
  `proto_epaper_esp32c3.ino` (`as` -> `ast` in the VDIV_CAL comment) - not
  part of this work, left out of the commit

**Next, in order:**

1. **Which sleeps are high?** CH3 on **D3 (GPIO5)**, ground clip on supply -.
   In phase 2 v3 holds D3 high through sleep; in phases 0/1 it floats. Log
   **6 min** (two full phase cycles) with CH2 + CH3. High sleeps only in phase 2
   -> a pin-hold / firmware effect. High sleeps in any phase -> the C3
   sometimes misses its deep-sleep state.
2. **D0:** remove the 10k, 3 min log - should match D1b.
3. **Flash `sleep_probe` v1** (part 1's version, from git) on the same board and
   log. Back at ~25 uA -> v3 raises the sleep current, and every sculpture
   result taken with v3 (P3, U1, U2) needs rereading.
4. Only then back to the sculpture and the *Candidates* list above - the bare
   board result moves suspect #2 (the XIAO itself) and the firmware up, and the
   modules down.

```
python tools/scope_log.py --ip 192.168.94.12 --channel 3,2 --interval 0.25 --duration 360 --out sleep_current/data/<date>/D1c_phase.csv
```

Sleep current = (CH2 median - 1.08 mV) / 10.1 ohm.

### Previous handoff - 2026-10-04

**Sculpture state, as left:**

* running **`sleep_probe` v3**, *not* the node firmware - it does not update the
  panel. Back to normal needs a reflash of `proto_epaper_esp32c3` v1.4.
* settings in NVS, kept across the reflashes: **`cycle_s` 60, `park_epd` 1**.
  Set `cycle_s=300` (and `park_epd` as decided) before normal running.
* flash log erased 2026-10-03 evening
* jumper cap **off**, lab supply + on pin X at 4.5 V, − through the 10.1 ohm
  shunt to the frame, 1N5817 removed, panels covered
* **cap at 3.10 V - below `v_floor` 3.25 V.** The node firmware would hold
  without drawing anything. Charge it through pin V (S1) before a node-firmware
  test, or before closing the jumper for normal running.
* scope 192.168.94.12: CH1 100 mV/div offset -300 mV, **probe on 10x** (read
  x10); CH2 **5 mV/div offset -15 mV** (changed for U1), probe 1x; 50 ms/div,
  average
* everything soldered back as built (BME280 VCC restored, GND reflowed)

**Done since:** BME280 VCC unsoldered (U1): current went *up* to ~1.4-1.8 mA
and drifted - the breakout back-powered through SDA/SCL. Soldered back and its
GND reflowed (U2): ~0.86 mA again, unchanged. SDA and SCL read high during
sleep. **The sculpture is fully reconnected now.**

**Unsolder VCC alone and the part back-powers through its signal lines; that is
what spoiled U1.** Take VCC *and* GND off together and there is no return path,
so nothing can feed it backwards.

**Next, first:** take a zero (wire across the shunt) at CH2's current setting,
**5 mV/div, offset -15 mV**. Then either:

* **BME280: unsolder VCC and GND** (SDA/SCL can stay), or
* **e-paper: unsolder VCC.** In `sleep_probe` phases 0 and 1 its lines are
  released; in phase 2 RST and CS are on ~45k pull-ups, which can back-feed at
  most ~60 uA through its input diodes - small against 0.86 mA.

**Next: split the hardware by unsoldering, one joint at a time**, `sleep_probe`
running, supply on pin X, 2 min log each:

| Step | Unsolder | Falls to ~25-50 uA means |
| ---- | -------- | ------------------------ |
| 1 | e-paper VCC wire | the e-paper module |
| 2 | BME280 VCC wire | the BME280 breakout |
| 3 | still high with both off | the sculpture's XIAO, or the wiring |

```
python tools/scope_log.py --ip 192.168.94.12 --channel 1,2 --interval 0.25 --duration 120 --out sleep_current/data/<date>/U<step>.csv
```

Sleep current = (CH2 median - 0.34 mV) / 10.1 ohm.

### Previous handoff - 2026-10-03, evening


**Sculpture state, as left:**

* firmware **v1.3**, settings `cycle_s` **60** (the other four at defaults), flash
  log **erased** at ~19:00, so it now holds only this session's wakes
* jumper cap **off**; lab supply + on pin X at 4.5 V, − through the 10.1 ohm
  shunt to the frame; **1N5817 removed**; panels covered; cap at ~3.83 V
* scope: CH1 100 mV/div offset -300 mV, CH2 2 mV/div offset -6 mV, both 1x in
  the scope, 50 ms/div, average. **The CH1 probe's switch is on 10x** - set it
  to 1x, or read CH1 as x10

**Measured so far:** sleep ~0.9 mA, wake ~15 s at ~21 mA (see S2 above). Not
zeroed with the wire, which is fine at this size: the zero is worth ~30 uA.

**Next, in order:**

1. **CH1 probe to 1x**, then a clean 5 min log at 4.5 V:
   `python tools/scope_log.py --ip 192.168.94.12 --channel 1,2 --interval 0.25 --duration 300 --out sleep_current/data/<date>/S2_4V5.csv`
   and `analyze.py cycle ... --ohms 10.1 --wake-offset-mv <CH1 zero> --offset-mv 0.34`.
2. **The same at 3.8 V supply.** Does the ~0.9 mA depend on supply voltage? The
   night's 400 uA was at 3.4-2.8 V.
3. **S3, the e-paper lines held through sleep** - firmware only. If the sleep
   current falls back towards ~25 uA, the e-paper's floating inputs were it.
4. If S3 does not help: the BME280 breakout (purple, 4-pin - likely has its own
   regulator and level shifters) is the remaining suspect. Separating it without
   soldering needs a firmware trick, e.g. not parking SDA/SCL, or an S3-style
   variant for the I2C lines.
5. Before normal running again: `python tools/node_cfg.py cycle_s=300`, jumper
   cap back on, USB off.
