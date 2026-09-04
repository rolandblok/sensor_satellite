// sensor_satellite - prototype 2
// ESP32-C3 SuperMini + BME280 (I2C) + Waveshare 2.9" b/w e-paper (SPI, SSD1680)
// Also builds for a Seeed XIAO ESP32-C3 - set BOARD_XIAO below. See gpio_xiao.md.
//
// No <WiFi.h> on purpose. This node never uses WiFi or BLE, and the Arduino
// core does not power the RF PHY until something calls WiFi.*/BLE*. Including
// <WiFi.h> just to call WiFi.mode(WIFI_OFF) links the whole WiFi stack for
// identical power behaviour - see proto_oled_esp32c3.md.

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_BME280.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <esp_sleep.h>
#include <driver/gpio.h>

// ---------------- config ----------------
#define PANEL_V2        1      // 1 = Waveshare 2.9" V2 (SSD1680, "V2" on the back)
                               // 0 = original V1 (IL3820)
#define USE_DEEP_SLEEP  1      // 1 = sleep between cycles; drops the USB serial port
#define CYCLE_S         300    // seconds between refreshes - keep >= 180 for e-paper
#define LOG_S           2      // serial log interval when not deep sleeping
#define ALTITUDE_M      17.0f  // Eindhoven, ~17 m AMSL - for sea-level pressure
#define MIN_REFRESH_C   0.0f   // below this the panel is skipped, image is kept

// ---------------- board / pin map ----------------
// Both boards are the same silicon. The XIAO brings out 11 GPIO against the
// SuperMini's 13 - GPIO0 and GPIO1 are not bonded out - so the I2C bus is the
// only thing that moves. Everything else keeps its pin. See gpio_xiao.md.
#define BOARD_XIAO      1      // 0 = ESP32-C3 SuperMini (what this build runs on)
                               // 1 = Seeed XIAO ESP32-C3

// e-paper (SPI) and VSENSE. Identical on both boards; the XIAO silk-screens the
// same GPIO under D-numbers, which do not correspond in any regular way - D6 is
// GPIO21 and D7 is GPIO20, adjacent on the chip and opposite sides of the board.
// Wire from the GPIO number, not the silk.
//
// DC is on GPIO21, not GPIO3: GPIO3 is the only ADC1 channel left for VSENSE.
// GPIO21 is UART0 TX, free here because Serial is USB-CDC on GPIO18/19.
#define EPD_CS    7            // XIAO D5
#define EPD_DC    21           // XIAO D6
#define EPD_RST   5            // XIAO D3
#define EPD_BUSY  10           // XIAO D10
#define EPD_SCK   4            // XIAO D2
#define EPD_MOSI  6            // XIAO D4
#define EPD_MISO  -1           // MUST be -1: the default MISO is GPIO5, used by RST

// I2C bus, pinned rather than auto-detected - the sweep would otherwise drive
// the VSENSE tap as a bus line. Set both to -1 to sweep again; the candidate
// list is board-specific, see "bus discovery" below.
//
// UART mirror of the log. Serial is native USB-CDC and disappears the moment
// USB is unplugged - which is exactly when the node runs from the cap. A
// listener board on the other end keeps the log alive - see logger_d1_mini/.
#if BOARD_XIAO
  // SCL takes the strapping pin, not SDA. SCL is master-driven and nothing but
  // a short can hold it low; SDA can be held low by a slave hung mid-transaction
  // through a reset, and a strapping pin low at reset is a board that will not
  // boot. The breakout's bus pull-up is what satisfies the strapping - internal
  // pulls are not dependable in the sampling window before software runs.
  #define FORCE_SDA      20    // D7, U0RXD - an input, silent through reset
  #define FORCE_SCL      2     // D0 - strapping, held high by the bus pull-up
  // GPIO20 is SDA here, so it cannot also be the mirror TX. Dropping the mirror
  // is what frees the pin: without it this build does not fit. The replacement
  // is logging to the C3's own flash, which is NOT WRITTEN YET - until it is,
  // the XIAO build has no log once USB is unplugged, so no cap-power runs.
  #define USE_LOG_MIRROR 0
  #define LOG_TX_PIN     -1
#else
  #define FORCE_SDA      0
  #define FORCE_SCL      1
  // GPIO20 is U0RXD, free because Serial is USB-CDC, used here as UART0 TX.
  #define USE_LOG_MIRROR 1
  #define LOG_TX_PIN     20
#endif
#define LOG_BAUD   115200

// Supercapacitor sense. 1 Mohm / 1 Mohm divider with 100 nF at the tap, on the
// one ADC1 channel this build has spare. GPIO2 would have been the obvious pin
// and is wrong: it is a strapping pin that must be high at reset, and a divider
// on it holds it low whenever the cap is flat - a dead board, not a bad reading.

// Pins parked before deep sleep.  Entering deep sleep releases the digital
// pads to high-Z, so a level driven here does not survive without a hold; and
// an unconnected input floating near mid-rail burns shoot-through current.
// GPIO8 is both a strapping pin (must be HIGH at boot) and the data line of
// the onboard WS2812B pixel, which draws ~1 mA from 3V3 in every state. It is
// left alone here - see parkPins() below.
#define PARK_PINS       1

#define VSENSE_PIN 3       // XIAO D1 - ADC1_3 on both boards
#define VDIV_NUM   2.0f    // (R3+R4)/R4
#define VDIV_CAL   1.0149f // 2026-08-28: DMM 4.81 V vs 4.7962 V, mean of 5 boots
                           // (spread 4.782-4.811, so this is good to ~0.3%)

#if PANEL_V2
  #define EPD_CLASS GxEPD2_290_T94_V2
#else
  #define EPD_CLASS GxEPD2_290
#endif

GxEPD2_BW<EPD_CLASS, EPD_CLASS::HEIGHT> display(
    EPD_CLASS(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

Adafruit_BME280 bme;

// survives deep sleep, lost on power cycle / reset button
RTC_DATA_ATTR uint32_t bootCount = 0;
RTC_DATA_ATTR float    tMin      =  999.0f;
RTC_DATA_ATTR float    tMax      = -999.0f;

static int8_t  sdaPin = -1, sclPin = -1;
static uint8_t bmeAddr = 0;
static bool    bmeOk  = false;

// Must precede the first function definition: the Arduino preprocessor injects
// generated prototypes there, and they reference this type.
struct Reading {
  float tC, rh, hPa, hPaSea, dewC;
  float vcap;
};

// ---------------- logging ----------------
// Everything goes to both ports. Cheap insurance: a line that only reaches USB
// is a line that does not exist during a cap run.
static void logBoth(const char *fmt, ...) {
  char buf[192];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
#if ARDUINO_USB_CDC_ON_BOOT
  // Only a separate port when Serial is native USB-CDC. Built with
  // CDCOnBoot=default, Serial IS UART0 - the same peripheral Serial0 drives -
  // and printing to both duplicates every line on GPIO20.
  Serial.print(buf);
#endif
#if USE_LOG_MIRROR
  Serial0.print(buf);
#endif
}

// Progress marker. Flushes, because the point is to survive a hang in the
// very next call - anything left in the TX FIFO would be lost.
static void mark(const char *what) {
  logBoth("# mark: %s\n", what);
#if USE_LOG_MIRROR
  Serial0.flush();
#endif
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.flush();
#endif
  delay(15);                   // let the last byte clear the shift register
}

// ---------------- bus discovery ----------------
// Only used when FORCE_SDA/FORCE_SCL are -1; forced pins skip the sweep entirely.
// GPIO18/19 are USB and GPIO21 is e-paper DC on both boards. GPIO3 is excluded
// too: driving the VSENSE tap as a bus line fights the divider.
#if BOARD_XIAO
// GPIO0/GPIO1 do not exist on this board, and GPIO20 is free to sweep because
// there is no log mirror on it - it is where SDA actually lives.
static const uint8_t PINS[] = {2, 4, 5, 6, 7, 8, 9, 10, 20};
#else
// GPIO20 is the log mirror here, so it is not a candidate.
static const uint8_t PINS[] = {0, 1, 2, 4, 5, 6, 7, 8, 9, 10};
#endif
static const uint8_t NPINS  = sizeof(PINS) / sizeof(PINS[0]);

static bool probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static bool startBus(int8_t sda, int8_t scl) {
  Wire.end();
  Wire.setPins(sda, scl);
  if (!Wire.begin()) return false;
  Wire.setClock(100000);
  delay(5);
  return true;
}

// Finds the pair carrying the BME280. Skips pins claimed by the e-paper.
static bool findBus() {
  if (FORCE_SDA >= 0 && FORCE_SCL >= 0) {
    sdaPin = FORCE_SDA; sclPin = FORCE_SCL;
    if (!startBus(sdaPin, sclPin)) return false;
    // Still probe: bmeAddr is what begin() needs, and pinning the pins says
    // nothing about which of the two addresses the SDO strap selected.
    for (uint8_t a = 0x76; a <= 0x77; a++) {
      if (probe(a)) { bmeAddr = a; return true; }
    }
    return false;
  }
  for (uint8_t i = 0; i < NPINS; i++) {
    for (uint8_t j = 0; j < NPINS; j++) {
      if (i == j) continue;
      if (!startBus(PINS[i], PINS[j])) continue;
      for (uint8_t a = 0x76; a <= 0x77; a++) {
        if (probe(a)) {
          sdaPin = PINS[i]; sclPin = PINS[j]; bmeAddr = a;
          return true;
        }
      }
    }
  }
  return false;
}

// ---------------- sensor ----------------
static bool bmeBegin() {
  if (!bme.begin(bmeAddr, &Wire)) {
    logBoth("# BME280 found at 0x%02X but begin() failed\n", bmeAddr);
    return false;
  }
  uint8_t id = bme.sensorID();
  logBoth("# sensor ID 0x%02X at 0x%02X", id, bmeAddr);
  if      (id == 0x60) logBoth("  (BME280, has humidity)\n");
  else if (id == 0x58) logBoth("  (BMP280 - NO humidity, RH is fiction)\n");
  else                 logBoth("  (unrecognised)\n");

  // Bosch "weather monitoring" profile: one forced conversion per cycle,
  // no oversampling, no filter. Avoids the self-heating of normal mode.
  bme.setSampling(Adafruit_BME280::MODE_FORCED,
                  Adafruit_BME280::SAMPLING_X1,
                  Adafruit_BME280::SAMPLING_X1,
                  Adafruit_BME280::SAMPLING_X1,
                  Adafruit_BME280::FILTER_OFF);
  return true;
}

// Magnus-Tetens dew point, good to ~0.4 C over 0..60 C
static float dewPoint(float tC, float rh) {
  const float a = 17.62f, b = 243.12f;
  if (rh <= 0.0f) return NAN;
  float g = logf(rh / 100.0f) + (a * tC) / (b + tC);
  return (b * g) / (a - g);
}

static bool bmeRead(Reading &r) {
  if (!bmeOk) return false;
  if (!bme.takeForcedMeasurement()) return false;

  r.tC  = bme.readTemperature();
  r.rh  = bme.readHumidity();
  r.hPa = bme.readPressure() / 100.0f;
  if (isnan(r.tC) || isnan(r.hPa)) return false;

  r.hPaSea = r.hPa / powf(1.0f - (ALTITUDE_M / 44330.0f), 5.255f);
  r.dewC   = dewPoint(r.tC, r.rh);
  return true;
}

// ---------------- supercap sense ----------------
// Millivolts, not raw counts: the C3's efuse ADC calibration is doing real work
// at these levels. 12 dB attenuation is calibrated to roughly 2.5 V at the pin,
// so the tap is trustworthy to about Vcap 4.8 V and compresses above it - read
// anything higher as "high", not as a number. The first conversion after a
// pin/attenuation change is unsettled, so it is discarded.
static float readVcap() {
  analogSetPinAttenuation(VSENSE_PIN, ADC_11db);   // 3.x alias for ADC_ATTEN_DB_12
  (void)analogReadMilliVolts(VSENSE_PIN);
  uint32_t acc = 0;
  for (int i = 0; i < 32; i++) acc += analogReadMilliVolts(VSENSE_PIN);
  return acc / 32.0f * VDIV_NUM * VDIV_CAL / 1000.0f;
}

// ---------------- display ----------------
static void drawRight(const char *s, int16_t xRight, int16_t y) {
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(s, 0, y, &bx, &by, &bw, &bh);
  display.setCursor(xRight - bw, y);
  display.print(s);
}

static void drawFrame(bool ok, const Reading &r) {
  char buf[32];
  const int16_t W = display.width();     // 296 in landscape
  const int16_t H = display.height();    // 128

  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);

  // header
  display.setFont(&FreeSans9pt7b);
  display.setCursor(4, 15);
  display.print("sensor satellite");
  snprintf(buf, sizeof(buf), "%.2f V   #%lu", r.vcap, (unsigned long)bootCount);
  drawRight(buf, W - 4, 15);
  display.drawFastHLine(0, 21, W, GxEPD_BLACK);

  if (!ok) {
    display.setFont(&FreeSansBold9pt7b);
    display.setCursor(4, 60);
    display.print("NO SENSOR");
    display.setFont(&FreeSans9pt7b);
    display.setCursor(4, 84);
    display.print("BME280 not on I2C");
    display.setCursor(4, 104);
    display.print("check CSB->3V3, SDO->GND, power");
    return;
  }

  // big temperature, left half
  display.setFont(&FreeSansBold24pt7b);
  snprintf(buf, sizeof(buf), "%.1f", r.tC);
  display.setCursor(4, 72);
  display.print(buf);
  display.setFont(&FreeSansBold9pt7b);
  display.print(" C");

  // right column
  const int16_t xr = W - 4;
  display.setFont(&FreeSans9pt7b);
  snprintf(buf, sizeof(buf), "%.0f %% RH", r.rh);     drawRight(buf, xr, 44);
  snprintf(buf, sizeof(buf), "%.0f hPa", r.hPaSea);   drawRight(buf, xr, 66);
  snprintf(buf, sizeof(buf), "dew %.1f C", r.dewC);   drawRight(buf, xr, 88);

  // footer
  display.drawFastHLine(0, H - 22, W, GxEPD_BLACK);
  display.setFont(&FreeSans9pt7b);
  display.setCursor(4, H - 6);
  snprintf(buf, sizeof(buf), "min %.1f   max %.1f", tMin, tMax);
  display.print(buf);
}

// Full refresh. At CYCLE_S >= 180 s this is within spec and avoids the
// ghosting bookkeeping that partial updates need.
static void refresh(bool ok, const Reading &r) {
  mark("refresh entered");
  display.setFullWindow();
  display.firstPage();
  do { drawFrame(ok, r); } while (display.nextPage());
  display.hibernate();      // drop the panel's HV rails - required between updates
}

// ---------------- logging ----------------
static void logHeader() {
  logBoth("# t_s,T_C,RH_pct,dew_C,P_station_hPa,P_sea_hPa,Tmin_C,Tmax_C,Vcap_V\n");
}

static void logReading(const Reading &r) {
  logBoth("%.1f,%.2f,%.1f,%.1f,%.2f,%.2f,%.1f,%.1f,%.3f\n",
                millis() / 1000.0f,
                r.tC, r.rh, r.dewC, r.hPa, r.hPaSea, tMin, tMax, r.vcap);
}

static void runCycle() {
  Reading r = {};
  r.vcap = readVcap();          // before the sensor: valid even on a read failure
  if (!bmeRead(r)) {
    logBoth("# BME280 read failed - showing fault frame (Vcap %.3f V)\n", r.vcap);
    refresh(false, r);
    return;
  }

  if (r.tC < tMin) tMin = r.tC;
  if (r.tC > tMax) tMax = r.tC;
  logReading(r);

  // E-paper refresh is unreliable below freezing. The panel is bistable, so
  // keeping the previous image costs nothing but staleness.
  if (r.tC < MIN_REFRESH_C) {
    logBoth("# %.1f C below %.1f C - skipping refresh, keeping last image\n",
                  r.tC, MIN_REFRESH_C);
    return;
  }
  refresh(true, r);
}

// Parked pins are latched across deep sleep; the hold must be released before a
// pad can be driven again, or writes to it are silently ignored.
static void unparkPins() {
#if PARK_PINS
  // GPIO8 and GPIO2 are included unconditionally and defensively: an earlier
  // build latched GPIO8 high, and a hold lives in the RTC domain and survives a
  // reflash until cleared. On the XIAO GPIO2 is also FORCE_SCL, so it is
  // released twice - gpio_hold_dis() is idempotent, so that is harmless.
  const gpio_num_t held[] = {(gpio_num_t)8, (gpio_num_t)2,
                             (gpio_num_t)FORCE_SDA, (gpio_num_t)FORCE_SCL,
                             (gpio_num_t)EPD_BUSY};
  for (gpio_num_t p : held) gpio_hold_dis(p);
  gpio_deep_sleep_hold_dis();
#endif
}

// Park every pin that would otherwise float once the peripherals are asleep.
// Pull direction follows each line's idle level, so nothing fights the pull
// when the peripheral is connected.  GPIO2/GPIO8/GPIO9 are strapping pins and
// must never be pulled low.
static void parkPins() {
#if PARK_PINS
  // GPIO8 is deliberately NOT touched on either board. On the SuperMini Plus V2
  // it is the data line of a WS2812B RGB pixel, not an LED anode: the pixel's
  // controller runs off 3V3 whatever the pin does and costs ~1 mA even showing
  // black, so no pin state here saves anything - only desoldering it does. A
  // pull-up would just source into its input, and holding it LOW is worse: the
  // hold survives the wake reset and GPIO8 must be high at boot. On the XIAO the
  // pin carries nothing at all, and the same "leave it alone" applies.
  //
  // An earlier comment here called it a blue LED with 206 uA of drive current,
  // from the 2026-09-03 shunt session that was later thrown out for a ground
  // loop. That 206 uA is unexplained, not an LED. See gpio.md.
#if !BOARD_XIAO
  pinMode(2,  INPUT_PULLUP);                   // strapping, unconnected by design
#endif                                         // on the XIAO GPIO2 *is* FORCE_SCL
  pinMode(FORCE_SDA, INPUT_PULLUP);            // I2C idles high
  pinMode(FORCE_SCL, INPUT_PULLUP);            // XIAO: this is GPIO2, strapping
  pinMode(EPD_BUSY,  INPUT_PULLDOWN);          // BUSY idles low

  // A pull set by pinMode alone does not survive deep sleep - the digital
  // domain powers down. Latching is what makes the pull mean anything here.
#if !BOARD_XIAO
  gpio_hold_en((gpio_num_t)2);
#endif
  gpio_hold_en((gpio_num_t)FORCE_SDA);
  gpio_hold_en((gpio_num_t)FORCE_SCL);
  gpio_hold_en((gpio_num_t)EPD_BUSY);
  gpio_deep_sleep_hold_en();
#endif
}

void setup() {
  unparkPins();
  Serial.begin(115200);
#if USE_LOG_MIRROR
  // UART0 TX remapped to GPIO20. Must come before display.init(): UART0's
  // default TX is GPIO21, and init()'s pinMode() on DC is what takes GPIO21
  // back off the UART matrix afterwards.
  Serial0.begin(LOG_BAUD, SERIAL_8N1, -1, LOG_TX_PIN);
#endif
  delay(300);                  // let USB-CDC enumerate before the first print
  bootCount++;
  logBoth("\n# proto_epaper_esp32c3  boot #%lu\n", (unsigned long)bootCount);
  logBoth("# radios never initialised - WiFi and BLE PHY unpowered\n");
  mark("serial up");

  // Sensor first, but never fatal: the display must come up either way so a
  // fault is visible on the panel rather than only on a serial port nobody is
  // watching.
  mark("i2c scan enter");
  if (findBus()) {
    logBoth("# I2C bus: SDA=GPIO%d SCL=GPIO%d\n", sdaPin, sclPin);
    mark("bme begin enter");
    bmeOk = bmeBegin();
  } else {
    logBoth("# no BME280 on any pin pair - check CSB/SDO strapping and power\n");
  }

  mark("display.init enter");
  display.init(115200, true, 2, false);
  mark("display.init returned");
  // ESP32 needs SPI re-bound to our pins. MISO must be -1: its default is
  // GPIO5, which RST occupies. e-paper is write-only so MISO is not needed.
  SPI.end();
  SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
  display.setRotation(1);      // landscape, 296x128
  mark("spi rebound");
  logBoth("# e-paper %dx%d ready (%s)\n", display.width(), display.height(),
                PANEL_V2 ? "V2 / SSD1680" : "V1 / IL3820");

  logBoth("# Vcap sense: GPIO%d, divider x%.2f, cal %.3f -> %.3f V now\n",
                VSENSE_PIN, VDIV_NUM, VDIV_CAL, readVcap());

  logHeader();

#if USE_DEEP_SLEEP
  mark("runCycle enter");
  runCycle();
  logBoth("# sleeping %d s\n", CYCLE_S);
  Serial.flush();
#if USE_LOG_MIRROR
  Serial0.flush();
#endif
  mark("parkPins enter");
  parkPins();
  mark("parkPins returned - sleeping now");
  esp_sleep_enable_timer_wakeup((uint64_t)CYCLE_S * 1000000ULL);
  esp_deep_sleep_start();      // does not return; setup() runs again on wake
#endif
}

void loop() {
#if !USE_DEEP_SLEEP
  runCycle();
  delay((uint32_t)CYCLE_S * 1000UL);
#endif
}
