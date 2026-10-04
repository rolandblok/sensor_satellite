// sleep_probe - what does an ESP32-C3 node draw in deep sleep?
// Seeed XIAO ESP32-C3. Runs on the bare breadboard board (part 1) and on the
// sculpture itself (part 2), where the BME280 and e-paper are soldered on.
// See ../README.md for the shunt, the scope settings and the test matrix.
//
// FQBN: esp32:esp32:XIAO_ESP32C3:CDCOnBoot=default   (default = CDC *enabled*)
//
// v3 (2026-10-04): commands the two peripherals into their lowest state, in
// phases of PHASE_WAKES wakes each, to find which of them holds the
// sculpture's ~0.9 mA. Which phase a sleep belongs to is coded in how long the
// wake before it lasted - visible as the width of the burst on the scope:
//
//   phase 0  0.5 s  nothing: e-paper lines released, BME280 untouched
//   phase 1  1.5 s  BME280 told to sleep (ctrl_meas = 0, one I2C write)
//   phase 2  2.5 s  BME280 sleep, plus e-paper: hardware reset, then the
//                   SSD1680 deep sleep command, then RST held HIGH through
//                   sleep - a low RST would reset it straight back out
//
// then back to phase 0, which releases RST again: if phase 2 helped, phase 0
// should undo it. The BME280 stays asleep once told, so phase 0's second
// time round tests the e-paper alone.
//
// Why: v2 showed the e-paper's line states move the sleep current by only
// ~0.1 mA, and with this sketch never touching either part the sculpture still
// slept at ~0.86 mA - once at ~0.34 mA. Two levels like that look like a part
// that is sometimes in its low-power state and sometimes not. The node pre-v1.4
// sends the e-paper's deep sleep after fixed delays, since the BUSY pad broke
// on 2026-09-18; if the panel was still busy, it never took it.
//
// On a cold boot with USB attached it prints its settings once, and what each
// command did. On the lab supply there is no USB and nothing prints.

#include <esp_sleep.h>
#include <driver/gpio.h>
#include <Wire.h>

// ---------------- config ----------------
#define SLEEP_S      30    // sleep plateau length
#define PHASE_WAKES  2     // wakes per phase
#define N_PHASES     3
#define FW_TAG       "sleep_probe v3"

#define FORCE_SDA  20      // D7 - BME280 SDA
#define FORCE_SCL  2       // D0 - BME280 SCL, strapping pin
#define FREE_PIN   10      // D10 - ex-BUSY, pulled down on the node
#define EPD_SCK    4       // D2
#define EPD_RST    5       // D3
#define EPD_MOSI   6       // D4
#define EPD_CS     7       // D5
#define EPD_DC     21      // D6

RTC_DATA_ATTR uint32_t wakes = 0;

static const uint8_t EPD_PINS[] = {EPD_SCK, EPD_RST, EPD_MOSI, EPD_CS, EPD_DC};
static bool usb = false;

static void say(const char *fmt, ...) {
  if (!usb) return;
  char buf[96];
  va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
  Serial.print(buf);
}

// A hold lives in the RTC domain and survives a reflash, so release every pin
// either sketch ever latched before touching anything.
static void unparkPins() {
  const gpio_num_t held[] = {(gpio_num_t)8, (gpio_num_t)2, (gpio_num_t)FORCE_SDA,
                             (gpio_num_t)FORCE_SCL, (gpio_num_t)FREE_PIN};
  for (gpio_num_t p : held) gpio_hold_dis(p);
  for (uint8_t p : EPD_PINS) gpio_hold_dis((gpio_num_t)p);
  gpio_deep_sleep_hold_dis();
}

// BME280 ctrl_meas (0xF4) mode bits 00 = sleep. Tries both addresses.
static void bmeSleep() {
  Wire.begin(FORCE_SDA, FORCE_SCL);
  for (uint8_t a : {0x76, 0x77}) {
    Wire.beginTransmission(a);
    Wire.write(0xF4);
    Wire.write(0x00);
    const uint8_t err = Wire.endTransmission();
    say("# BME280 at 0x%02X: %s\n", a, err == 0 ? "told to sleep" : "no ACK");
  }
  Wire.end();
}

// Bit-banged SPI, mode 0, MSB first - a handful of bytes, no driver needed.
static void epdByte(bool data, uint8_t b) {
  digitalWrite(EPD_DC, data ? HIGH : LOW);
  digitalWrite(EPD_CS, LOW);
  for (int i = 7; i >= 0; i--) {
    digitalWrite(EPD_MOSI, (b >> i) & 1);
    digitalWrite(EPD_SCK, HIGH);
    digitalWrite(EPD_SCK, LOW);
  }
  digitalWrite(EPD_CS, HIGH);
}

// Hardware reset, then SSD1680 "Deep Sleep Mode" (0x10) with mode 1. No BUSY
// pin, so generous fixed waits instead.
static void epdDeepSleep() {
  for (uint8_t p : EPD_PINS) pinMode(p, OUTPUT);
  digitalWrite(EPD_CS, HIGH);
  digitalWrite(EPD_SCK, LOW);
  digitalWrite(EPD_RST, LOW);  delay(20);
  digitalWrite(EPD_RST, HIGH); delay(200);
  epdByte(false, 0x10);
  epdByte(true,  0x01);
  delay(100);
  say("# e-paper: reset, deep sleep command sent\n");
}

static void parkPins(uint8_t phase) {
  pinMode(FORCE_SDA, INPUT_PULLUP);
  pinMode(FORCE_SCL, INPUT_PULLUP);
  pinMode(FREE_PIN,  INPUT_PULLDOWN);
  gpio_hold_en((gpio_num_t)FORCE_SDA);
  gpio_hold_en((gpio_num_t)FORCE_SCL);
  gpio_hold_en((gpio_num_t)FREE_PIN);
  if (phase == 2) {
    // RST and CS high keep the panel asleep and deselected; the rest low.
    pinMode(EPD_RST, INPUT_PULLUP);    pinMode(EPD_CS, INPUT_PULLUP);
    pinMode(EPD_SCK, INPUT_PULLDOWN);  pinMode(EPD_MOSI, INPUT_PULLDOWN);
    pinMode(EPD_DC,  INPUT_PULLDOWN);
    for (uint8_t p : EPD_PINS) gpio_hold_en((gpio_num_t)p);
  }
  gpio_deep_sleep_hold_en();
}

void setup() {
  unparkPins();
  const uint8_t phase = (wakes / PHASE_WAKES) % N_PHASES;   // cold boot: phase 0
  wakes++;

  const bool cold = (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER);
  if (cold) {
    // ~0.5 s for a USB host to start sending SOFs; without one, skip Serial.
    for (int i = 0; i < 50 && !usb; i++) {
      usb = HWCDC::isPlugged();
      if (!usb) delay(10);
    }
    if (usb) {
      Serial.begin(115200);
      delay(1500);           // let the terminal attach
      say("\n# %s  sleep %d s, %d wakes per phase, %d phases\n",
          FW_TAG, SLEEP_S, PHASE_WAKES, N_PHASES);
    }
  }

  const uint32_t t0 = millis();
  if (phase >= 1) bmeSleep();
  if (phase >= 2) epdDeepSleep();

  // The burst's width says which phase follows: 0.5, 1.5, 2.5 s in total.
  const uint32_t want = 500 + 1000 * phase;
  const uint32_t spent = millis() - t0;
  if (spent < want) delay(want - spent);

  say("# phase %u, going to sleep\n", phase);
  if (usb) Serial.flush();
  parkPins(phase);
  esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_S * 1000000ULL);
  esp_deep_sleep_start();
}

void loop() {}
