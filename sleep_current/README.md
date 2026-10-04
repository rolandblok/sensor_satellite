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

## Where this left off - 2026-10-04

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
* **BME280 VCC wire unsoldered** (SDA, SCL, GND still on)

**Done since:** BME280 VCC unsoldered (U1) - current went *up* to ~1.4-1.8 mA
and drifted upwards, almost certainly because the BME280 is now back-powered
through SDA/SCL. **The sculpture is in that state now.**

**Next, first:** unsolder the BME280's SDA and SCL too (or the whole breakout),
then repeat U1. Take a zero (wire across the shunt) at CH2's new setting,
**5 mV/div, offset -15 mV**, which it was changed to for U1. Then continue
with the table below.

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
