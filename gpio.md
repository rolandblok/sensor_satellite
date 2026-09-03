# ESP32-C3 SuperMini — pinout

16 castellated pins, 8 per side. USB-C at one end, ceramic antenna at the
other. The module is an ESP32-C3FH4: single RISC-V core, 4 MB flash, native
USB (no serial chip), 11 usable GPIO.

Viewed from the top, component side up, USB-C at the top:

```
                              /--USB-C--\
  ADC2_0  MTDI  MISO  GPIO5   |         |   5V      VBUS, LDO in
          MTCK  MOSI  GPIO6   |         |   GND
          MTDO  SS    GPIO7   |         |   3V3     LDO out
   strap   LED        GPIO8   |         |   GPIO4   SCK   MTMS  ADC1_4
   strap   BOOT       GPIO9   |         |   GPIO3               ADC1_3
                      GPIO10  |         |   GPIO2   strap       ADC1_2
          U0RXD  RX   GPIO20  |         |   GPIO1   XTAL32K_N   ADC1_1
          U0TXD  TX   GPIO21  |         |   GPIO0   XTAL32K_P   ADC1_0
                              |  ((( )))|
                              \---------/
```

GPIO11–GPIO17 are flash and are not bonded out. GPIO18/GPIO19 are USB D−/D+
and go straight to the connector — they are not on the header either.

## Per pin

| Pin | ADC | Alternate | Notes |
| --- | --- | --------- | ----- |
| GPIO0 | ADC1_0 | XTAL_32K_P | free, safe |
| GPIO1 | ADC1_1 | XTAL_32K_N | free, safe |
| GPIO2 | ADC1_2 | — | **strapping** — must be HIGH at boot; left unconnected here |
| GPIO3 | ADC1_3 | — | free, safe — the only ADC channel this build has left |
| GPIO4 | ADC1_4 | SCK, MTMS | default SPI clock |
| GPIO5 | ADC2_0 | MISO, MTDI | default SPI MISO; ADC2 unusable with WiFi |
| GPIO6 | — | MOSI, MTCK | default SPI MOSI |
| GPIO7 | — | SS, MTDO | default SPI chip select |
| GPIO8 | — | — | **strapping** — must be HIGH at boot; onboard blue LED, **active HIGH** |
| GPIO9 | — | — | **strapping** — BOOT button; LOW at boot enters download mode |
| GPIO10 | — | — | free, safe, no ADC |
| GPIO20 | — | U0RXD | UART0 RX by default; used here as UART0 **TX** for the log mirror |
| GPIO21 | — | U0TXD | UART0 TX — free when `Serial` is USB-CDC, but the ROM boot log still prints here |

**The blue LED on GPIO8 is active HIGH, measured 2026-09-03.** This table said
active LOW until then, and it was wrong. The LED's anode is on the pin: driving
GPIO8 high lights it, high-Z and low both leave it dark. It was seen lit, so the
polarity is not in doubt; the current it costs is not yet reliably measured. So the pin wants leaving alone in sleep —
a pull-up would source current straight through the LED, and holding it low
would work but the hold survives the wake reset, and this pin must be high at
boot. Note the strapping requirement means the LED flashes briefly at every
boot; that is normal and not a fault.

Arduino's I²C default is `SDA=8, SCL=9`, which lands on both strapping pins.
Call `Wire.setPins()` before `Wire.begin()` and move it — this project uses
GPIO0/GPIO1.

## Power pins

| Pin | What it is |
| --- | ---------- |
| `5V` | Tied straight to USB VBUS, no diode. Input to the onboard LDO, and back-feeding it also back-feeds a connected USB host. |
| `3V3` | LDO output, roughly 300 mA. Can be fed directly to bypass the onboard regulator. |
| `GND` | Two pads, both the same net. |

Bench builds feed VCAP to `5V` and use the onboard LDO. The final solar build
feeds `3V3` from an external low-Iq LDO with the onboard regulator and power
LED desoldered — see [solar_node.md](solar_node.md).

## As wired in this build

```
                              /--USB-C--\
  e-paper RST ------- GPIO5   |         |   5V    ---- VCAP (USB unplugged)
  e-paper DIN ------- GPIO6   |         |   GND   ---- common ground
  e-paper CS  ------- GPIO7   |         |   3V3   ---- BME280 + e-paper VCC
         free ------- GPIO8   |         |   GPIO4 ---- e-paper CLK
         free ------- GPIO9   |         |   GPIO3 ---- VSENSE, Vcap divider tap
  e-paper BUSY ------ GPIO10  |         |   GPIO2 ---- deliberately unused
   log mirror ------- GPIO20  |         |   GPIO1 ---- BME280 SCL
  e-paper DC  ------- GPIO21  |         |   GPIO0 ---- BME280 SDA
                              |  ((( )))|
                              \---------/
```

| Device | Signal | C3 pin |
| ------ | ------ | ------ |
| BME280 | SDA | GPIO0 |
| BME280 | SCL | GPIO1 |
| Vcap divider | VSENSE tap | GPIO3 |
| e-paper | CLK | GPIO4 |
| e-paper | RST | GPIO5 |
| e-paper | DIN | GPIO6 |
| e-paper | CS | GPIO7 |
| e-paper | BUSY | GPIO10 |
| e-paper | DC | GPIO21 |
| logger | UART TX out | GPIO20 |

### The sense pin is GPIO3, not GPIO2

GPIO2 is a strapping pin and must be high at reset. A divider on it would hold
it at Vcap ÷ 2:

```
Vcap 4.6 V  ->  2.3 V  ->  boots
Vcap 2.0 V  ->  1.0 V  ->  marginal
Vcap 0.0 V  ->  0.0 V  ->  will not boot
```

Bench runs start from a flat cap, so the failure is the first thing that would
happen — and it presents as a dead board, not as a divider fault.

GPIO3 is ADC1_3 and is not a strapping pin. ADC1 on the C3 is GPIO0–GPIO4
only; GPIO0/GPIO1 are the I²C bus and GPIO4 is the SPI clock, so GPIO3 is the
one channel left. **GPIO2 is left deliberately unconnected.**

### DC moved to GPIO21

Freeing GPIO3 pushes e-paper DC onto GPIO21. That is safe here: `Serial` is
USB-CDC on GPIO18/GPIO19, so UART0 is never initialised and both GPIO20 and
GPIO21 are ordinary GPIO. DC needs nothing but a plain output.

One side effect: the ROM bootloader still prints its boot log on GPIO21 at
reset, so DC wiggles for a few milliseconds before the sketch starts. Harmless
— GxEPD2 hardware-resets the panel during `init()`, well after that — but it
is the reason not to put a level-sensitive signal like CS or RST there.

### Divider

1 MΩ / 1 MΩ with 100 nF at the tap: ÷2, 2.3 µA at 4.6 V, 50 ms settling.
100 kΩ / 100 kΩ would bleed 27 µA, comparable to the entire sleep budget.

The C3's ADC at 12 dB attenuation is calibrated to about 2.5 V, so the tap is
trustworthy to roughly Vcap 4.8 V and compresses above it — read higher values
as "high", not as a number. No damage risk: Vcap cannot exceed Voc − Vf ≈
6.2 V, and 1 MΩ limits clamp current to a couple of µA.

### Log mirror on GPIO20

`Serial` is native USB-CDC, so the log dies the moment USB is unplugged — which
is exactly when the node runs from the cap. UART0's TX is remapped to GPIO20 and
every log line goes to both ports. A listener board on the other end turns that
back into a COM port; see [`logger_d1_mini/`](logger_d1_mini/logger_d1_mini.ino).

One wire plus ground, and one direction only:

```
node GPIO20 ---[ 1k ]--- D7 (GPIO13) on the D1 mini
node GND --------------- GND
```

Nothing else. The node runs from the cap and the logger from USB, so a 3V3 or 5V
link between them back-feeds the cap and destroys the measurement the run exists
to make. Unidirectional leaves one back-feed path and the 1 kΩ caps it at µA.

The logger also carries a third wire — its own 300 kΩ from VCAP to A0 — as an
independent Vcap instrument that keeps reading below LDO dropout, and a small
OLED showing both readings. Wiring, the 300 kΩ reasoning, the display layout
and the calibration are in [solar_node.md](solar_node.md).

`Serial0.begin()` must run **before** `display.init()`: UART0's default TX is
GPIO21, and the `pinMode()` inside `init()` is what takes GPIO21 back off the
UART matrix for DC.

### What is left

GPIO8 and GPIO9, both strapping pins. Anything added later should hang off the
existing I²C bus, which costs no pins — BH1750 (`0x23`) for lux is the obvious
candidate.

## Gotchas

**`Serial` prints nothing until USB CDC is on.** No serial chip — `Serial` is
native USB. Build with `CDCOnBoot=cdc`, or the sketch runs but looks dead.

**MISO must be disabled or it fights RST.** The default mapping is
`SCK=4, MISO=5, MOSI=6, SS=7` and RST sits on GPIO5. E-paper is write-only, so
bind SPI with MISO off: `SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS)`.

**GPIO5 is ADC2, not ADC1.** ADC2 is claimed by the WiFi radio on the C3, so
treat GPIO0–GPIO4 as the only real analogue inputs. Several pinout images going
around label this pin `ADC1_5`; there is no such channel.

**The power LED is always on.** Most SuperMini boards fit one drawing 1–3 mA,
which is 30× the target sleep budget. Desolder it before the solar build.

Firmware detail and module strapping (BME280 `CSB`/`SDO`, e-paper `BS`) are in
[proto_epaper_esp32c3.md](proto_epaper_esp32c3.md).
