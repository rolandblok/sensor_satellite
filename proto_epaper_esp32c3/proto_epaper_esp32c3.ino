// sensor_satellite - prototype 2
// ESP32-C3 SuperMini + BME280 (I2C) + Waveshare 2.9" b/w e-paper (SPI, SSD1680)
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

// ---------------- config ----------------
#define PANEL_V2        1      // 1 = Waveshare 2.9" V2 (SSD1680, "V2" on the back)
                               // 0 = original V1 (IL3820)
#define USE_DEEP_SLEEP  0      // 1 = sleep between cycles; drops the USB serial port
#define CYCLE_S         300    // seconds between refreshes - keep >= 180 for e-paper
#define LOG_S           2      // serial log interval when not deep sleeping
#define ALTITUDE_M      17.0f  // Eindhoven, ~17 m AMSL - for sea-level pressure
#define MIN_REFRESH_C   0.0f   // below this the panel is skipped, image is kept

// e-paper pins (SPI). DIN/CLK/CS are the C3's native FSPI pins.
#define EPD_CS    7
#define EPD_DC    3
#define EPD_RST   5
#define EPD_BUSY  10
#define EPD_SCK   4
#define EPD_MOSI  6
#define EPD_MISO  -1           // MUST be -1: the default MISO is GPIO5, used by RST

// I2C bus is auto-detected. Set these to force fixed pins instead.
#define FORCE_SDA -1
#define FORCE_SCL -1

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
};

// ---------------- bus discovery ----------------
// GPIO18/19 are USB, GPIO20/21 are UART0 - leave them alone.
static const uint8_t PINS[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
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
    return startBus(sdaPin, sclPin);
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
    Serial.printf("# BME280 found at 0x%02X but begin() failed\n", bmeAddr);
    return false;
  }
  uint8_t id = bme.sensorID();
  Serial.printf("# sensor ID 0x%02X at 0x%02X", id, bmeAddr);
  if      (id == 0x60) Serial.println("  (BME280, has humidity)");
  else if (id == 0x58) Serial.println("  (BMP280 - NO humidity, RH is fiction)");
  else                 Serial.println("  (unrecognised)");

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
  snprintf(buf, sizeof(buf), "#%lu", (unsigned long)bootCount);
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
  display.setFullWindow();
  display.firstPage();
  do { drawFrame(ok, r); } while (display.nextPage());
  display.hibernate();      // drop the panel's HV rails - required between updates
}

// ---------------- logging ----------------
static void logHeader() {
  Serial.println("# t_s,T_C,RH_pct,dew_C,P_station_hPa,P_sea_hPa,Tmin_C,Tmax_C");
}

static void logReading(const Reading &r) {
  Serial.printf("%.1f,%.2f,%.1f,%.1f,%.2f,%.2f,%.1f,%.1f\n",
                millis() / 1000.0f,
                r.tC, r.rh, r.dewC, r.hPa, r.hPaSea, tMin, tMax);
}

static void runCycle() {
  Reading r = {};
  if (!bmeRead(r)) {
    Serial.println("# BME280 read failed - showing fault frame");
    refresh(false, r);
    return;
  }

  if (r.tC < tMin) tMin = r.tC;
  if (r.tC > tMax) tMax = r.tC;
  logReading(r);

  // E-paper refresh is unreliable below freezing. The panel is bistable, so
  // keeping the previous image costs nothing but staleness.
  if (r.tC < MIN_REFRESH_C) {
    Serial.printf("# %.1f C below %.1f C - skipping refresh, keeping last image\n",
                  r.tC, MIN_REFRESH_C);
    return;
  }
  refresh(true, r);
}

void setup() {
  Serial.begin(115200);
  delay(300);                  // let USB-CDC enumerate before the first print
  bootCount++;
  Serial.printf("\n# proto_epaper_esp32c3  boot #%lu\n", (unsigned long)bootCount);
  Serial.println("# radios never initialised - WiFi and BLE PHY unpowered");

  // Sensor first, but never fatal: the display must come up either way so a
  // fault is visible on the panel rather than only on a serial port nobody is
  // watching.
  if (findBus()) {
    Serial.printf("# I2C bus: SDA=GPIO%d SCL=GPIO%d\n", sdaPin, sclPin);
    bmeOk = bmeBegin();
  } else {
    Serial.println("# no BME280 on any pin pair - check CSB/SDO strapping and power");
  }

  display.init(115200, true, 2, false);
  // ESP32 needs SPI re-bound to our pins. MISO must be -1: its default is
  // GPIO5, which RST occupies. e-paper is write-only so MISO is not needed.
  SPI.end();
  SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
  display.setRotation(1);      // landscape, 296x128
  Serial.printf("# e-paper %dx%d ready (%s)\n", display.width(), display.height(),
                PANEL_V2 ? "V2 / SSD1680" : "V1 / IL3820");

  logHeader();

#if USE_DEEP_SLEEP
  runCycle();
  Serial.printf("# sleeping %d s\n", CYCLE_S);
  Serial.flush();
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
