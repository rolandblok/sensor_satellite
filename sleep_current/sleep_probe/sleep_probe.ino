// sleep_probe - what does a bare XIAO ESP32-C3 draw in deep sleep?
// Seeed XIAO ESP32-C3, nothing attached: no BME280, no e-paper, no divider.
// See ../README.md for the breadboard, the scope settings and the test matrix.
//
// FQBN: esp32:esp32:XIAO_ESP32C3:CDCOnBoot=default   (default = CDC *enabled*)
//
// The node firmware cannot answer this on its own: the 2026-10-03 panel run
// gave ~400 uA asleep, but only as 4 F x dV/dt with the cap's own leakage,
// the divider, the BME280 and the panel all lumped in. This sketch is the same
// sleep with everything else taken away, so the scope sees the board alone.
//
// What it does, forever:
//   wake -> park pins exactly as the node does -> stay awake AWAKE_MS -> sleep SLEEP_S
// The awake burst is deliberate. It is a ~20 mA marker every SLEEP_S seconds,
// so on the scope each sleep plateau has a clear start and end, and a logger
// sample that caught a wake is obvious and can be thrown away.
//
// On a cold boot with USB attached it prints its settings once, so you can
// check what is flashed. On the lab supply there is no USB and nothing prints.

#include <esp_sleep.h>
#include <driver/gpio.h>

// ---------------- config ----------------
#define SLEEP_S    60      // sleep plateau length; long, so most samples are sleep
#define AWAKE_MS   1000    // marker burst; CPU idles in delay(), ~20 mA
#define PARK_PINS  1       // 1 = park exactly as proto_epaper_esp32c3 v1.2 does
                           // 0 = leave everything floating; test whether it matters
#define FW_TAG     "sleep_probe v1"

// The node's pins, for parking. Nothing is connected to them here; the point is
// that the pads end up in the same state as on the sculpture.
#define FORCE_SDA  20      // D7 - BME280 SDA on the node
#define FORCE_SCL  2       // D0 - BME280 SCL on the node, strapping pin
#define FREE_PIN   10      // D10 - ex-BUSY, pulled down on the node

RTC_DATA_ATTR uint32_t wakes = 0;

// Same as the node: a hold lives in the RTC domain and survives a reflash, so
// release every pin the node ever latched before touching anything.
static void unparkPins() {
  const gpio_num_t held[] = {(gpio_num_t)8, (gpio_num_t)2, (gpio_num_t)FORCE_SDA,
                             (gpio_num_t)FORCE_SCL, (gpio_num_t)FREE_PIN};
  for (gpio_num_t p : held) gpio_hold_dis(p);
  gpio_deep_sleep_hold_dis();
}

// Copied from proto_epaper_esp32c3 parkPins(), XIAO branch. GPIO8 is left
// alone, as on the node.
//
// Without the BME280 breakout there is no external pull-up on GPIO2. The
// internal pull-up below is what holds the strapping pin high through the next
// wake; it is enough on a bare board with nothing pulling the line down.
static void parkPins() {
#if PARK_PINS
  pinMode(FORCE_SDA, INPUT_PULLUP);
  pinMode(FORCE_SCL, INPUT_PULLUP);
  pinMode(FREE_PIN,  INPUT_PULLDOWN);
  gpio_hold_en((gpio_num_t)FORCE_SDA);
  gpio_hold_en((gpio_num_t)FORCE_SCL);
  gpio_hold_en((gpio_num_t)FREE_PIN);
  gpio_deep_sleep_hold_en();
#endif
}

void setup() {
  unparkPins();
  wakes++;

  const bool cold = (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER);
  if (cold) {
    // ~0.5 s for a USB host to start sending SOFs; without one, skip Serial.
    bool usb = false;
    for (int i = 0; i < 50 && !usb; i++) {
      usb = HWCDC::isPlugged();
      if (!usb) delay(10);
    }
    if (usb) {
      Serial.begin(115200);
      delay(1500);           // let the terminal attach
      Serial.printf("\n# %s  sleep %d s, awake %d ms, park %d\n",
                    FW_TAG, SLEEP_S, AWAKE_MS, PARK_PINS);
      Serial.println("# going to sleep - the port disappears now");
      Serial.flush();
    }
  }

  delay(AWAKE_MS);           // the marker burst

  parkPins();
  esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_S * 1000000ULL);
  esp_deep_sleep_start();
}

void loop() {}
