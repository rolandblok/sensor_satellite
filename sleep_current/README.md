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

**Part 2, the integrated sculpture: next.** The missing ~375 uA is somewhere
on the sculpture, or in its cap. Part 2 measures it there with no soldering:
the VCAP jumper is the break point. It measures the wake as well as the sleep.

## Contents

| Path | What |
| ---- | ---- |
| `sleep_probe/sleep_probe.ino` | firmware for the breadboard XIAO: parks pins like the node, wakes for 1 s, sleeps 60 s, forever |
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
| **S2** | pin X, 4.50 V, cap at ~4.0 V from S1, 20 min | ~260 mC per wake; sleep somewhere between 25 and 400 uA | the sculpture's wake cost and sleep current, directly | |
| **S3** | as S2, firmware holding the e-paper lines through sleep | | are the panel's floating inputs the extra sleep current? | only if S2's sleep is far above 25 uA |

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
