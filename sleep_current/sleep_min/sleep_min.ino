// sleep_min - the least an ESP32-C3 can do: wake, wait 2 s, deep sleep 15 s.
// No Serial, no pins, no peripherals, no holds. A baseline for the Seeed XIAO
// ESP32-C3's sleep current: anything above it is not the firmware's doing.
//
// FQBN: esp32:esp32:XIAO_ESP32C3:CDCOnBoot=cdc   (on this board cdc = USB CDC
// *disabled*, so the core does not start Serial either). Nothing prints; to
// reflash, do the BOOT dance: hold BOOT, tap RESET, release BOOT.
//
// Variant A (2026-10-16): the same code built with CDCOnBoot=default (CDC
// *enabled*), to test whether the core's USB CDC alone brings back v3's
// 0.4 / 3 mA sleeps. Still prints nothing - the sketch never writes to Serial.
// Result (N2): it does not.
//
// Variant B: PARK 1 adds sleep_probe v3's phase 0 parkPins() verbatim - pull-ups
// held on GPIO20 and GPIO2, pull-down held on GPIO10, then all holds kept
// through deep sleep. Built with CDCOnBoot=cdc again, so only this changes.
// Result (N3): both of v3's levels come back, 0.51 / ~3 mA.
//
// To find which hold does it: PARK_MASK picks the pins (1 = GPIO20, 2 = GPIO2,
// 4 = GPIO10; default 7 = all three, as N3), PARK_GLOBAL 0 leaves out
// gpio_deep_sleep_hold_en(). E.g. -DPARK=1 -DPARK_MASK=2.
//
// Power-cycle after flashing: a GPIO hold from an earlier sketch lives in the
// RTC domain and survives a reflash, and with PARK 0 this sketch deliberately
// does not touch the pins to clear it.

#include <esp_sleep.h>
#include <driver/gpio.h>

#ifndef PARK
#define PARK 0   // 1 = variant B; or build with -DPARK=1
#endif
#ifndef PARK_MASK
#define PARK_MASK 7
#endif
#ifndef PARK_GLOBAL
#define PARK_GLOBAL 1
#endif

#if PARK
static void parkPins() {
  if (PARK_MASK & 1) {
    pinMode(20, INPUT_PULLUP);    // D7, BME280 SDA on the node
    gpio_hold_en((gpio_num_t)20);
  }
  if (PARK_MASK & 2) {
    pinMode(2, INPUT_PULLUP);     // D0, BME280 SCL on the node, strapping pin
    gpio_hold_en((gpio_num_t)2);
  }
  if (PARK_MASK & 4) {
    pinMode(10, INPUT_PULLDOWN);  // D10, ex-BUSY
    gpio_hold_en((gpio_num_t)10);
  }
  if (PARK_GLOBAL) gpio_deep_sleep_hold_en();
}
#endif

void setup() {
#if PARK
  // as v3: release last cycle's holds before anything else
  gpio_hold_dis((gpio_num_t)20);
  gpio_hold_dis((gpio_num_t)2);
  gpio_hold_dis((gpio_num_t)10);
  gpio_deep_sleep_hold_dis();
#endif
  delay(2000);
#if PARK
  parkPins();
#endif
  esp_sleep_enable_timer_wakeup(15ULL * 1000000ULL);
  esp_deep_sleep_start();
}

void loop() {}
