#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_BME280.h>
#include <esp_sleep.h>

// No <WiFi.h> on purpose. This node never uses WiFi or BLE, and the Arduino
// core does not power the RF PHY until something calls WiFi.*/BLE*. Simply not
// touching them leaves both radios unpowered. Including <WiFi.h> just to call
// WiFi.mode(WIFI_OFF) links the whole WiFi stack - ~650 KB of flash, 50% of the
// image - for identical power behaviour. If a library is ever added that starts
// the radio on its own, add the explicit shutdown back and pay the flash.

// ---------------- config ----------------
#define LOG_S           2      // seconds between serial log lines (bench mode)
#define USE_DEEP_SLEEP  0      // keep 0 while developing - sleep drops USB serial
#define CYCLE_S         300    // sleep length when USE_DEEP_SLEEP is 1
#define DISPLAY_MS      5000   // OLED on-time per cycle, deep-sleep mode only
#define ALTITUDE_M      17.0f  // Eindhoven, ~17 m AMSL - for sea-level pressure

// Bus is auto-detected at startup. Set these to force fixed pins instead.
#define FORCE_SDA       -1
#define FORCE_SCL       -1

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
Adafruit_BME280 bme;

// survives deep sleep, lost on power cycle / reset button
RTC_DATA_ATTR uint32_t bootCount = 0;
RTC_DATA_ATTR float    tMin      =  999.0f;
RTC_DATA_ATTR float    tMax      = -999.0f;

static int8_t  sdaPin = -1, sclPin = -1;
static uint8_t bmeAddr = 0;
static bool    bmeOk  = false;
static bool    oledOk = false;

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
  Wire.setPins(sda, scl);       // must precede begin(); u8g2 reuses these
  if (!Wire.begin()) return false;
  Wire.setClock(100000);
  delay(5);
  return true;
}

// Finds the pair carrying the BME280. Returns false if nothing answers.
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
                  Adafruit_BME280::SAMPLING_X1,   // temperature
                  Adafruit_BME280::SAMPLING_X1,   // pressure
                  Adafruit_BME280::SAMPLING_X1,   // humidity
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

  // reduce station pressure to sea level
  r.hPaSea = r.hPa / powf(1.0f - (ALTITUDE_M / 44330.0f), 5.255f);
  r.dewC   = dewPoint(r.tC, r.rh);
  return true;
}

// ---------------- display (skipped if no OLED on the bus) ----------------
static void showReading(bool ok, const Reading &r) {
  if (!oledOk) return;
  char buf[24];
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 10, "sensor satellite");
  snprintf(buf, sizeof(buf), "#%lu", (unsigned long)bootCount);
  u8g2.drawStr(128 - u8g2.getStrWidth(buf), 10, buf);
  u8g2.drawHLine(0, 13, 128);

  if (!ok) {
    u8g2.drawStr(0, 34, "BME280 read failed");
    u8g2.sendBuffer();
    return;
  }

  u8g2.setFont(u8g2_font_7x14B_tr);
  u8g2.drawStr(0, 30, "T");   snprintf(buf, sizeof(buf), "%.1f C",   r.tC);     u8g2.drawStr(32, 30, buf);
  u8g2.drawStr(0, 46, "RH");  snprintf(buf, sizeof(buf), "%.0f %%",  r.rh);     u8g2.drawStr(32, 46, buf);
  u8g2.drawStr(0, 62, "P");   snprintf(buf, sizeof(buf), "%.0f hPa", r.hPaSea); u8g2.drawStr(32, 62, buf);

  u8g2.sendBuffer();
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
  bool ok = bmeRead(r);

  if (ok) {
    if (r.tC < tMin) tMin = r.tC;
    if (r.tC > tMax) tMax = r.tC;
    logReading(r);
  } else {
    Serial.println("# BME280 read failed");
  }
  showReading(ok, r);
}

void setup() {
  Serial.begin(115200);
  delay(300);                  // let USB-CDC enumerate before the first print
  bootCount++;
  Serial.printf("\n# proto_oled_esp32c3  boot #%lu\n", (unsigned long)bootCount);

  Serial.println("# radios never initialised - WiFi and BLE PHY unpowered");

  if (!findBus()) {
    Serial.println("# no BME280 on any pin pair - check CSB/SDO strapping and power");
    return;
  }
  Serial.printf("# I2C bus: SDA=GPIO%d SCL=GPIO%d\n", sdaPin, sclPin);

  bmeOk = bmeBegin();

  // OLED is optional - the rig runs headless until it is wired
  if (probe(0x3C) || probe(0x3D)) {
    if (probe(0x3D)) u8g2.setI2CAddress(0x3D << 1);
    u8g2.begin();
    oledOk = true;
    Serial.println("# OLED found");
  } else {
    Serial.println("# no OLED - serial only");
  }

  logHeader();

#if USE_DEEP_SLEEP
  runCycle();
  if (oledOk) { delay(DISPLAY_MS); u8g2.setPowerSave(1); }
  Serial.printf("# sleeping %d s\n", CYCLE_S);
  Serial.flush();
  esp_sleep_enable_timer_wakeup((uint64_t)CYCLE_S * 1000000ULL);
  esp_deep_sleep_start();      // does not return; setup() runs again on wake
#endif
}

void loop() {
#if !USE_DEEP_SLEEP
  if (!bmeOk) { delay(1000); return; }
  runCycle();
  delay((uint32_t)LOG_S * 1000UL);
#endif
}
