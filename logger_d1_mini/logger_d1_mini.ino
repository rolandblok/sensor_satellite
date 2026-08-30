// sensor_satellite - UART witness logger
// Wemos D1 mini (ESP8266). Listens to the ESP32-C3 node, re-prints what it
// hears on its own USB port, and shows both Vcap measurements on a small OLED.
//
// Why this exists: the node's Serial is native USB-CDC, so its log vanishes the
// instant USB is unplugged - which is exactly when it runs from the
// supercapacitor and exactly when the log matters. The node mirrors every line
// out of GPIO20 as plain UART. This board turns that back into a COM port.
//
// Wiring - to the node, three wires:
//
//   node GPIO20 ---[ 1k ]--- D7 (GPIO13)     one way: node talks, this listens
//   node GND --------------- GND             common reference, mandatory
//   VCAP --------[ 300k ]--- A0              independent Vcap witness
//
// Do NOT link 3V3 or 5V between the boards. The node runs from the cap, this
// runs from USB; a supply link back-feeds the cap and destroys the very
// measurement the run exists to make. Ground and signals only.
//
// Wiring - to the OLED (SSD1306 128x64, I2C), four wires, all local:
//
//   OLED VCC ---- 3V3        OLED SDA ---- D2 (GPIO4)
//   OLED GND ---- GND        OLED SCL ---- D1 (GPIO5)
//
// The OLED runs entirely off this board's USB-fed 3V3 and never touches VCAP.
// It draws 10-20 mA, which would be fatal to a 4 F cap and is free on USB.
// D1/D2 are the ESP8266 Wire defaults and match the prototype 0 bench rig; they
// also avoid every strapping pin (D3/GPIO0, D4/GPIO2, D8/GPIO15).
//
// Why SoftwareSerial rather than the hardware UART: the ESP8266's only UART
// with an RX is on GPIO1/GPIO3, and those are hardwired to the CH340. Letting
// the node drive GPIO3 would put two drivers on one line. UART1 is TX-only. So
// reception has to be soft, and the hardware Serial stays on USB duty.
//
// The ESP8266 has no hardware I2C either - Wire is bit-banged, and a full
// 128x64 frame is 1024 bytes, roughly 25-30 ms of blocking transfer. The redraw
// therefore only runs at a line boundary, so a node CSV row is never cut in
// half, and at most once a second. If bytes are ever lost anyway the overflow
// line below says so; the fix then is U8G2 page-buffer mode.

// The A0 witness branch:
//
//   VCAP --[300k]--+-- A0 --[220k]--+--[100k]-- GND    (220k/100k are onboard)
//                                   |
//                                  ADC 0-1 V
//
// This must be its own 300k from VCAP. Do NOT tap the node's 1M/1M divider:
// A0's onboard network is 320k to ground, which in parallel with the lower 1M
// leg drags the ratio from 0.500 to 0.195 - one wire, both instruments wrong.
//
// Worth having because the node dies at LDO dropout, ~3.6 V, and everything
// below that is invisible to it: the bottom of the charge curve before it can
// boot, and the brownout itself. This board runs from USB, so it watches the
// cap from 0 V up and from 3.6 V down.
//
// 300k (two 150k in series) is the right value here, not a round number: it
// puts full scale at 1.0 V * 620/100 = 6.20 V, which is exactly Voc - Vf, the
// highest voltage the cap can physically reach. So the whole ADC range is used
// and it never clips - and A0 sees 3.20 V at that ceiling, exactly its rating.
//
// Costs 7.4 uA at 4.6 V through the 620k chain. Fine next to a 50-100 uA
// leakage floor for charge and discharge runs; pull the wire for a sleep
// current measurement, where it would be a third of the figure being measured.

#include <ESP8266WiFi.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <U8g2lib.h>

#define RX_PIN     D7        // GPIO13: not a strapping pin, supports interrupts
                             // (D0/GPIO16 does not), and is where Serial.swap()
                             // maps hardware UART0 RX - so if soft serial ever
                             // proves too slow, the upgrade needs no rewiring.
#define NODE_BAUD  115200    // must match LOG_BAUD in the node's sketch
#define USB_BAUD   115200
#define RX_BUF     1024      // node bursts a whole CSV line at a time

#define VCAP_SENSE 1         // 0 = pure pass-through, no A0 wire fitted
#define VCAP_SCALE 0.005721f // 2026-08-28: DMM 4.81 V vs 4.8068 V reported.
                             // 0.006061 = (1.0/1023)*(620/100). The 5.7% trim is
                             // the resistor and ADC-reference stack, as expected.
#define VCAP_MS    1000      // own-reading interval
#define VCAP_AVG   16

#define OLED_SDA   D2
#define OLED_SCL   D1
#define DISP_MS    1000      // redraw interval
#define NODE_PERIOD_S 300   // starting guess only; the real period is learned
#define NODE_STALE_S 400     // node logs every CYCLE_S (300 s by default), so
                             // silence past this means brownout or a lost wire
#define OLED_RETRY_MS 5000   // re-probe interval after the display drops off

// This panel is a dual-colour SSD1306: rows 0-15 emit yellow, rows 16-63 blue.
// That split is fixed in the glass, not addressable, so the layout has to
// respect it - anything straddling y=16 comes out half one colour and half the
// other and reads as a fault. The header lives entirely above the line, all the
// numbers entirely below it.
#define BAND_Y     16        // first blue row; nothing may cross it

#define OLED_CONTRAST 255  // full brightness; lower it if it glares

U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
SoftwareSerial nodeIn;

static uint32_t lastVcap    = 0;
static uint32_t lastDisp    = 0;
static bool     atLineStart = true;   // never interleave into a node line
static bool     oledOk      = false;
static uint8_t  oledAddr    = 0;
static uint32_t lastOledTry = 0;

// Last Vcap the node reported, parsed out of the stream being relayed.
static float    nodeVcap    = NAN;
static uint32_t nodeSeen    = 0;
static uint32_t nodePeriod  = NODE_PERIOD_S;   // seconds between node rows

static char     lineBuf[128];
static uint8_t  lineLen     = 0;

static float readVcap() {
  uint32_t acc = 0;
  for (uint8_t i = 0; i < VCAP_AVG; i++) acc += analogRead(A0);
  return (acc / (float)VCAP_AVG) * VCAP_SCALE;
}

// The node's CSV is t_s,T_C,RH,dew,P,Psea,Tmin,Tmax,Vcap_V - nine fields, Vcap
// last. Anything else (banners, line noise, a half-received row) is ignored.
static void parseNodeLine() {
  lineBuf[lineLen] = '\0';
  if (lineLen < 20 || lineBuf[0] == '#') return;

  uint8_t commas = 0;
  int16_t last   = -1;
  for (uint8_t i = 0; i < lineLen; i++)
    if (lineBuf[i] == ',') { commas++; last = i; }
  if (commas != 8 || last < 0) return;

  float v = atof(lineBuf + last + 1);
  if (v <= 0.05f || v >= 10.0f) return;

  // Learn the node's reporting period rather than hard-coding CYCLE_S: the
  // countdown then stays honest when CYCLE_S is changed on the node.
  if (nodeSeen) {
    const uint32_t iv = (millis() - nodeSeen) / 1000;
    if (iv >= 1 && iv <= 3600) nodePeriod = iv;
  }
  nodeVcap = v;
  nodeSeen = millis();
}

// Probes both addresses SSD1306 modules ship with, so a board strapped to 0x3D
// works with no code change. Returns 0 if nothing answers.
static uint8_t findOled() {
  static const uint8_t addrs[] = { 0x3C, 0x3D };
  for (uint8_t i = 0; i < sizeof(addrs); i++) {
    Wire.beginTransmission(addrs[i]);
    if (Wire.endTransmission() == 0) return addrs[i];
  }
  return 0;
}

// One-shot bus diagnostic. Idle levels first, because they separate the two
// failure modes that look identical from the address scan alone: both lines
// high means pull-ups are present and powered, so the bus is alive and nothing
// is answering; either line low means no pull-ups, an unpowered module, or a
// short - and no address will ever ACK.
// Is anything actually driving D7? Drive it low, release, and sample: an idle
// UART TX on the far end pulls it back high through the 1k in nanoseconds,
// while a floating pin holds the low charge for milliseconds. Distinguishes a
// disconnected wire from a silent node, which look identical from the stream.
static void rxLinkReport() {
  pinMode(RX_PIN, OUTPUT);
  digitalWrite(RX_PIN, LOW);
  delayMicroseconds(50);
  pinMode(RX_PIN, INPUT);
  delayMicroseconds(20);
  const int lvl = digitalRead(RX_PIN);
  Serial.printf("# node link: D7 %s%s\n",
                lvl ? "HIGH" : "LOW",
                lvl ? "  (node TX idle, wire present)"
                    : "  <- nothing driving D7, check the wire");
}

static void busReport() {
  pinMode(OLED_SDA, INPUT);
  pinMode(OLED_SCL, INPUT);
  delay(2);
  const int sda = digitalRead(OLED_SDA), scl = digitalRead(OLED_SCL);
  Serial.printf("# I2C idle: SDA=%s SCL=%s%s\n",
                sda ? "HIGH" : "LOW", scl ? "HIGH" : "LOW",
                (sda && scl) ? "  (bus alive)" : "  <- no pull-ups or no power");

  Wire.begin(OLED_SDA, OLED_SCL);
  uint8_t found = 0;
  for (uint8_t a = 0x08; a <= 0x77; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("# I2C device at 0x%02X%s\n", a,
                    (a == 0x3C || a == 0x3D) ? "  (SSD1306)" : "");
      found++;
    }
  }
  if (!found) Serial.println("# I2C scan: nothing answered on 0x08-0x77");
}

static bool oledAnswers() {
  if (!oledAddr) return false;
  Wire.beginTransmission(oledAddr);
  return Wire.endTransmission() == 0;
}

static bool initOled() {
  oledAddr = findOled();
  if (!oledAddr) return false;
  oled.setI2CAddress(oledAddr << 1);       // u8g2 wants the 8-bit form
  oled.begin();
  oled.setContrast(OLED_CONTRAST);
  oledOk = true;
  return true;
}

static void drawScreen(float lv) {
  const bool     known = !isnan(nodeVcap);
  const uint32_t age   = known ? (millis() - nodeSeen) / 1000 : 0;
  char buf[28];

  oled.clearBuffer();

  // --- yellow band, rows 0-15. 9x15 bold is the tallest cell that fits the
  //     band whole: baseline 12 puts the glyph top at row 1 and the descender
  //     at row 15, so nothing bleeds into the blue. Countdown to the node's
  //     next row, and the difference between the two instruments.
  oled.setFont(u8g2_font_9x15B_tr);
  if (!known) {
    snprintf(buf, sizeof(buf), "no node yet");
  } else if (age > NODE_STALE_S) {
    snprintf(buf, sizeof(buf), "NODE QUIET");
  } else {
    const uint32_t left = (age < nodePeriod) ? nodePeriod - age : 0;
    const int      dmv  = (int)lroundf((lv - nodeVcap) * 1000.0f);
    snprintf(buf, sizeof(buf), "%lus  %+dmV", (unsigned long)left, dmv);
  }
  oled.drawStr(0, 12, buf);

  // --- blue band, the live reading. logisoso24 at baseline 42 puts the top at
  //     row 18, clear of BAND_Y.
  oled.setFont(u8g2_font_logisoso24_tr);
  snprintf(buf, sizeof(buf), "%.3f V", lv);
  oled.drawStr(0, 42, buf);

  // --- blue band, the node's last word. Baseline 61, ascent 11, so top is 50
  //     and it clears the line above.
  oled.setFont(u8g2_font_9x15B_tr);
  if (known) snprintf(buf, sizeof(buf), "node %.3f V", nodeVcap);
  else       snprintf(buf, sizeof(buf), "node --.--- V");
  oled.drawStr(0, 61, buf);

  oled.sendBuffer();
}

void setup() {
  Serial.begin(USB_BAUD);
  delay(200);

  // The ESP8266 SDK brings the radio up from persisted config whether or not
  // the sketch asks. Nothing here needs it, and it adds noise to the ADC.
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);         // active low on this board: off

  rxLinkReport();          // before nodeIn.begin() claims the pin
  nodeIn.begin(NODE_BAUD, SWSERIAL_8N1, RX_PIN, -1, false, RX_BUF);

  Serial.println();
  Serial.printf("# logger_d1_mini - listening on D7 (GPIO13) at %d 8N1\n", NODE_BAUD);
  Serial.println("# node output passes through verbatim; own readings are tagged 'L,'");
#if VCAP_SENSE
  Serial.printf("# L,t_s,Vcap_V   (A0 via 300k, scale %.6f V/count)\n", VCAP_SCALE);
#endif

  busReport();
  Wire.setClock(400000);
  Wire.setClockStretchLimit(1500);         // bound how long a sick slave stalls us
  if (initOled()) Serial.printf("# OLED at 0x%02X on SDA=D2 SCL=D1\n", oledAddr);
  // Never fatal: relaying the node's log is this board's real job, and a loose
  // display wire must not be able to take that down.
  else Serial.println("# no OLED on 0x3C or 0x3D - continuing without a display");
}

void loop() {
  while (nodeIn.available()) {
    char c = (char)nodeIn.read();
    Serial.write(c);
    atLineStart = (c == '\n');

    if (c == '\n' || c == '\r') { parseNodeLine(); lineLen = 0; }
    else if (lineLen < sizeof(lineBuf) - 1)     { lineBuf[lineLen++] = c; }

    // Toggle per line so the board shows traffic with no terminal attached.
    // Non-blocking on purpose: a delay here would drop bytes at 115200.
    if (c == '\n') digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }

  // Silence is ambiguous - it means either "node asleep" or "wire fell off".
  // An overflow is not ambiguous, so it is worth saying out loud.
  if (nodeIn.overflow()) Serial.println("# logger: RX overflow, bytes lost");

#if VCAP_SENSE
  // Only at a line boundary: the node's CSV must never be cut in half. This
  // keeps logging after the node browns out, which is the whole point.
  if (atLineStart && millis() - lastVcap >= VCAP_MS) {
    lastVcap = millis();
    Serial.printf("L,%.1f,%.3f\n", millis() / 1000.0f, readVcap());
  }

  // Same guard, and for a stronger reason: a redraw blocks for ~25 ms.
  if (atLineStart && millis() - lastDisp >= DISP_MS) {
    lastDisp = millis();
    if (oledOk) {
      // Cheap ACK probe first. A display on a loose wire would otherwise stall
      // a 1024-byte bit-banged transfer and take the relay down with it - which
      // is exactly what happened on the bench.
      if (oledAnswers()) drawScreen(readVcap());
      else {
        oledOk = false;
        lastOledTry = millis();
        Serial.println("# OLED stopped answering - relay continues without it");
      }
    } else if (millis() - lastOledTry >= OLED_RETRY_MS) {
      lastOledTry = millis();
      if (initOled()) Serial.printf("# OLED back at 0x%02X\n", oledAddr);
    }
  }
#endif
}
