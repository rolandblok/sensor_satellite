## Prototype 0 — D1 mini + OLED bench rig

First build with the parts on hand, before the ESP32-C3 / e-paper / solar hardware arrives.

Parts available now:

* Wemos D1 mini (ESP8266)
* 5.5 V 4 F supercapacitor (Kamcap)
* Asair DHT20 (I²C temperature + humidity)
* 1N5819 Schottky diode
* 128×64 mono I²C OLED (SSD1306)

Still missing: ESP32-C3, e-paper, solar panel, BME280, BH1750, low-Iq regulator.

---

### What this prototype is for

It proves out the parts of the design that carry over unchanged:

* I²C sensor read path and data conversion
* display layout and update cycle
* wake → measure → display → sleep firmware skeleton
* supercapacitor charge path through the Schottky diode
* real measured current draw, to replace guesses with numbers

It deliberately does **not** test the energy concept. An always-on OLED draws
~15 mA continuously; the e-paper it will replace draws ~0 between refreshes.
This rig will run for minutes on the supercap, not hours. That is expected —
see the power section below.

---

### Wiring

```
   USB 5V (or bench supply)
          |
         22R          <- inrush limiter, see "Charging" below
          |
       1N5819
     (band = out)
          |
          +--------------------+
          |                    |
     4F supercap          D1 mini "5V" pin
     5.5V, + to rail           |
          |              onboard 3.3V LDO
         GND                   |
                          3V3 rail
                          /        \
                     DHT20        SSD1306 OLED
                       |              |
                    SDA/SCL  <----> SDA/SCL   (shared I2C bus)
```

Pin table:

| Signal        | D1 mini pin | GPIO   | Goes to                          |
| ------------- | ----------- | ------ | -------------------------------- |
| SDA           | D2          | GPIO4  | DHT20 SDA + OLED SDA             |
| SCL           | D1          | GPIO5  | DHT20 SCL + OLED SCL             |
| 3V3           | 3V3         | —      | DHT20 VDD + OLED VCC             |
| GND           | G           | —      | DHT20 GND + OLED GND + supercap − |
| Cap rail      | 5V          | —      | supercap + / diode cathode       |
| Sleep wake    | D0          | GPIO16 | RST (only if deep sleep enabled)  |
| Cap sense     | A0          | ADC    | optional, via 270 k — see below   |

Notes:

* Both breakout modules carry their own I²C pull-ups (usually 4.7 k). Two in
  parallel is ~2.3 k — fine at 100 kHz, no extra resistors needed.
* Addresses do not collide: DHT20 = `0x38`, SSD1306 = `0x3C` (some modules `0x3D`).
* **The D0 → RST wire must be removed while flashing.** It pulls RST during the
  reset pulse and uploads fail with a sync error. Leave it off until the sketch
  is working, then add it and set `USE_DEEP_SLEEP 1`.
* Feeding the `5V` pin while USB is also plugged in is normal on this board —
  the USB input is diode-isolated — but the supercap will then be held at USB
  voltage and you cannot see it discharge. Unplug USB to test runtime.

Optional supercap voltage sense: the D1 mini's A0 already has an onboard
220 k / 100 k divider (0–3.2 V at the pin, 0–1 V at the ADC). Adding a **270 k**
resistor from the cap rail to A0 extends the range to ~5.9 V:

```
Vcap --[270k]--+-- A0 pin --[220k]--+--[100k]-- GND
                                    |
                                   ADC (0-1V)

Vcap = raw * (1.0/1023) * (590/100) = raw * 0.005767
```

Calibrate `VCAP_SCALE` in the sketch against a multimeter — resistor tolerance
and the ESP8266 ADC reference both drift several percent.

---

### Charging the supercapacitor

A discharged 4 F cap is electrically a short circuit. Connected straight to
USB 5 V it will trip the port's current limit or brown out the D1 mini on every
power-up. The 22 Ω in series fixes this:

* peak current = (5 − 0.3) / 22 ≈ **215 mA**
* time constant τ = R·C = 22 × 4 = **88 s**
* practically full after 5τ ≈ **7 minutes**

Keep the resistor for bench work. When the 5 V / 200 mA panel arrives it is
current-limited by its own I-V curve and the resistor can be dropped — leaving
it in wastes ~0.8 V at the 35 mA active load.

Other constraints:

* Never exceed 5.4 V on the cap. A 5.5 V 4 F part is two 2.7 V cells in series;
  if yours has no balancing resistors the cells will drift and one will overvolt.
* Charged through the 1N5819 the rail sits at ~5 V − 0.3 V ≈ **4.65 V**.
* Supercap self-leakage is 10–30 µA once charged. That is not negligible here —
  it is comparable to the sleep current of a well-designed node, and it is a
  reason the final build wants a bigger, lower-leakage bank rather than a
  lower-power MCU alone.

---

### Power reality check

The energy figure in `project.md` (≈ 60 J, 17 mWh) is ½CV² from 5.5 V down to
0 V. That is not reachable: the regulator drops out long before 0 V, and with a
linear LDO the useful quantity is **charge**, not energy.

D1 mini regulator (RT9013/ME6211 class) drops out around **3.6 V**.

```
usable charge   Q = C x dV = 4 F x (4.65 - 3.6) V = 4.2 C
delivered energy    = 4.2 C x 3.3 V = 13.9 J = 3.9 mWh
```

The remaining ~4 J is burned as heat in the LDO. Runtime at 3.3 V:

| State                                  | Current  | Runtime on a full cap |
| -------------------------------------- | -------- | --------------------- |
| ESP active (WiFi off) + OLED on        | ~35 mA   | **~2 minutes**        |
| Deep sleep, stock D1 mini              | ~0.3 mA  | ~3.9 hours            |
| 5 s awake per 300 s cycle              | ~0.9 mA  | **~1.3 hours**        |

Two things to take from this:

1. **The OLED is the whole power budget.** Duty-cycle it hard, or accept a
   two-minute battery. This is exactly the argument for e-paper.
2. **Deep sleep on a stock D1 mini is ~0.3 mA, not µA.** The CH340 USB-serial
   chip and the LDO's quiescent current dominate, and the ESP8266's own 20 µA
   sleep current is irrelevant next to them. The µA numbers in `project.md`
   apply to a bare ESP32-C3 module on a low-Iq regulator, not to a dev board
   with a USB bridge on it. Measure this yourself with a meter in series on the
   3V3 rail before trusting either figure.

Also worth knowing: a mono OLED showing static text at high contrast will
burn in within weeks of continuous use. Another reason the display should be
off most of the time.

---

### Display layout

The DHT20 gives temperature and humidity only. Pressure and lux wait for the
BME280 and BH1750, so the four-field layout from `project.md` shrinks to two:

```
+------------------------+
| sensor satellite       |
|                        |
| TEMP                   |
| 21.8 C                 |
|                        |
| HUM                    |
| 56 %                   |
+------------------------+
```

---

### Firmware

Arduino IDE / arduino-cli with the ESP8266 core. One library to install:
**U8g2** (olikraus). The DHT20 is driven directly over `Wire` — it is a simple
enough protocol that a library dependency is not worth it, and doing it by hand
documents the conversion maths for the port to ESP32-C3.

Save as `proto_oled_d1_mini/proto_oled_d1_mini.ino`:

```cpp
#include <Wire.h>
#include <U8g2lib.h>
#include <ESP8266WiFi.h>

// ---------------- config ----------------
#define USE_DEEP_SLEEP  0      // 1 = deep sleep between cycles; needs D0 -> RST
#define CYCLE_S         300    // seconds between measurements
#define DISPLAY_MS      5000   // how long the OLED stays lit each cycle
#define MEASURE_VCAP    0      // 1 = read cap voltage on A0 (needs 270k mod)
#define VCAP_SCALE      0.005767f  // volts per ADC count - calibrate this

static const uint8_t DHT20_ADDR = 0x38;

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ---------------- DHT20 ----------------
static uint8_t crc8(const uint8_t *d, uint8_t n) {
  uint8_t crc = 0xFF;
  for (uint8_t i = 0; i < n; i++) {
    crc ^= d[i];
    for (uint8_t b = 0; b < 8; b++)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
  }
  return crc;
}

static bool dht20Begin() {
  delay(100);                             // datasheet: 100 ms after power-up
  Wire.beginTransmission(DHT20_ADDR);
  Wire.write(0x71);                       // read status
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(DHT20_ADDR, (uint8_t)1) != 1) return false;
  uint8_t status = Wire.read();
  if ((status & 0x18) != 0x18) {          // calibration bits clear -> init
    Wire.beginTransmission(DHT20_ADDR);
    Wire.write(0xBE); Wire.write(0x08); Wire.write(0x00);
    Wire.endTransmission();
    delay(10);
  }
  return true;
}

static bool dht20Read(float &tC, float &rh) {
  Wire.beginTransmission(DHT20_ADDR);
  Wire.write(0xAC); Wire.write(0x33); Wire.write(0x00);   // trigger measurement
  if (Wire.endTransmission() != 0) return false;
  delay(80);                                              // conversion time
  if (Wire.requestFrom(DHT20_ADDR, (uint8_t)7) != 7) return false;
  uint8_t d[7];
  for (uint8_t i = 0; i < 7; i++) d[i] = Wire.read();
  if (d[0] & 0x80) return false;                          // still busy
  if (crc8(d, 6) != d[6]) return false;

  uint32_t hRaw = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
  uint32_t tRaw = (((uint32_t)d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
  rh = hRaw * 100.0f / 1048576.0f;          // 2^20
  tC = tRaw * 200.0f / 1048576.0f - 50.0f;
  return true;
}

// ---------------- display ----------------
static void showReading(bool ok, float tC, float rh, float vcap) {
  char buf[24];
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 10, "sensor satellite");
  u8g2.drawHLine(0, 13, 128);

  if (!ok) {
    u8g2.drawStr(0, 34, "DHT20 read failed");
  } else {
    u8g2.drawStr(0, 27, "TEMP");
    u8g2.setFont(u8g2_font_logisoso16_tf);
    snprintf(buf, sizeof(buf), "%.1f C", tC);
    u8g2.drawStr(40, 32, buf);

    u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.drawStr(0, 50, "HUM");
    u8g2.setFont(u8g2_font_logisoso16_tf);
    snprintf(buf, sizeof(buf), "%.0f %%", rh);
    u8g2.drawStr(40, 55, buf);
  }

  if (MEASURE_VCAP) {
    u8g2.setFont(u8g2_font_6x12_tf);
    snprintf(buf, sizeof(buf), "%.2fV", vcap);
    u8g2.drawStr(96, 10, buf);
  }
  u8g2.sendBuffer();
}

// ---------------- cycle ----------------
static void runCycle() {
  float tC = 0, rh = 0, vcap = 0;
  bool ok = dht20Read(tC, rh);

  if (MEASURE_VCAP) vcap = analogRead(A0) * VCAP_SCALE;

  if (ok) Serial.printf("T=%.2f C  RH=%.1f %%  Vcap=%.2f V\n", tC, rh, vcap);
  else    Serial.println("DHT20 read failed");

  u8g2.setPowerSave(0);
  showReading(ok, tC, rh, vcap);
  delay(DISPLAY_MS);
  u8g2.setPowerSave(1);          // OLED off - this is the power budget
}

void setup() {
  WiFi.mode(WIFI_OFF);           // saves ~50 mA; nothing here needs the radio
  WiFi.forceSleepBegin();

  Serial.begin(115200);
  delay(50);
  Serial.println("\nproto_oled_d1_mini");

  Wire.begin();                  // D2 = SDA (GPIO4), D1 = SCL (GPIO5)
  Wire.setClock(100000);
  u8g2.begin();

  if (!dht20Begin()) Serial.println("DHT20 not responding at 0x38");

  runCycle();

#if USE_DEEP_SLEEP
  Serial.printf("sleeping %d s\n", CYCLE_S);
  Serial.flush();
  ESP.deepSleep((uint64_t)CYCLE_S * 1000000ULL, WAKE_RF_DISABLED);
#endif
}

void loop() {
#if !USE_DEEP_SLEEP
  delay((uint32_t)CYCLE_S * 1000UL - DISPLAY_MS);
  runCycle();
#endif
}
```

---

### Bring-up order

Do these in sequence — each step isolates one failure mode.

1. **USB power only, no supercap.** Run an I²C scanner sketch. Expect `0x38`
   and `0x3C`. Nothing found → check SDA/SCL swap; only one found → that
   module's pull-ups or power are wrong.
2. **Flash the sketch, USB powered.** Confirm plausible readings on serial and
   OLED. Breathe on the DHT20; humidity should jump within a couple of cycles.
   A flat 0 % or −50 °C means the CRC check is passing on stale data — check
   the 80 ms conversion delay.
3. **Measure current.** Meter in series with the 3V3 rail, one reading with the
   OLED on and one with it off. Write the numbers down; they replace every
   estimate in the table above.
4. **Add the supercap and 22 Ω.** Charge ~7 minutes, verify ~4.6 V on the cap.
5. **Unplug USB and time it.** Record how long until the display dies. If it
   is far off ~2 minutes, the difference is your real active current or a
   high-ESR cap.
6. **Enable deep sleep.** Remove the D0 → RST wire, flash with
   `USE_DEEP_SLEEP 1`, then fit the wire. Re-measure sleep current.

---

### Carrying forward

Numbers to capture here that decide the real build:

| Measurement                        | Why it matters                                |
| ---------------------------------- | --------------------------------------------- |
| Active current, radio off          | sizes the panel and the wake duty cycle        |
| Deep sleep current                 | decides whether a low-Iq regulator is enough   |
| Supercap voltage after 12 h idle   | reveals real leakage; sizes the storage bank   |
| Cap droop during a WiFi TX burst   | reveals ESR; a bad cap browns out the MCU      |
| Time from flat to usable on charge | sets cold-start behaviour after a dark spell   |

Code that ports unchanged to the ESP32-C3: the DHT20 driver, the conversion
maths, the wake→measure→display→sleep structure. Code that does not: `Wire.begin()`
pin defaults, `ESP.deepSleep`, and the whole display layer once e-paper
partial refresh enters the picture.
