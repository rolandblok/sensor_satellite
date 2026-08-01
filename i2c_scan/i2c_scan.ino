// I2C bus finder for the sensor_satellite prototypes.
//
// Pass 1: sweep every plausible SDA/SCL pin pair on the ESP32-C3 and probe the
//         handful of addresses this project uses. Finds the bus no matter how
//         it was wired (e.g. GPIO8/9 if wired to the SuperMini silkscreen).
// Pass 2: full 0x08..0x77 scan on whichever pair worked.

#include <Wire.h>

// GPIO18/19 are USB, GPIO20/21 are UART0 - leave them alone.
static const uint8_t PINS[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
static const uint8_t NPINS  = sizeof(PINS) / sizeof(PINS[0]);

static const uint8_t KNOWN[] = {0x23, 0x38, 0x3C, 0x3D, 0x5C, 0x76, 0x77};
static const uint8_t NKNOWN  = sizeof(KNOWN) / sizeof(KNOWN[0]);

static const char *name(uint8_t a) {
  switch (a) {
    case 0x23: case 0x5C: return "BH1750 (light)";
    case 0x38:            return "DHT20 / AHT20";
    case 0x3C: case 0x3D: return "SSD1306 OLED";
    case 0x76: case 0x77: return "BME280 / BMP280";
    default:              return "unknown";
  }
}

static bool probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("\ni2c_scan - sweeping pin pairs");
}

void loop() {
  int8_t goodSda = -1, goodScl = -1;

  for (uint8_t i = 0; i < NPINS && goodSda < 0; i++) {
    for (uint8_t j = 0; j < NPINS; j++) {
      if (i == j) continue;
      uint8_t sda = PINS[i], scl = PINS[j];

      Wire.begin(sda, scl);
      Wire.setClock(100000);
      delay(5);

      for (uint8_t k = 0; k < NKNOWN; k++) {
        if (probe(KNOWN[k])) {
          Serial.printf("HIT  SDA=GPIO%-2u SCL=GPIO%-2u -> 0x%02X  %s\n",
                        sda, scl, KNOWN[k], name(KNOWN[k]));
          goodSda = sda; goodScl = scl;
        }
      }
      Wire.end();
      if (goodSda >= 0) break;
    }
  }

  if (goodSda < 0) {
    Serial.println("no I2C device on any pin pair");
    Serial.println("  -> check 3V3 and GND to both modules first");
    Serial.println("  -> then check SDA/SCL are not both on the same pin");
    delay(5000);
    return;
  }

  Serial.printf("\nfull scan on SDA=GPIO%d SCL=GPIO%d:\n", goodSda, goodScl);
  Wire.begin(goodSda, goodScl);
  Wire.setClock(100000);
  uint8_t found = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    if (probe(addr)) { Serial.printf("  0x%02X  %s\n", addr, name(addr)); found++; }
    delay(2);
  }
  Serial.printf("  %u device(s)\n\n", found);
  Wire.end();
  delay(8000);
}
