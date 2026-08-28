// sensor_satellite - UART witness logger
// Wemos D1 mini (ESP8266). Listens to the ESP32-C3 node and re-prints what it
// hears on its own USB port.
//
// Why this exists: the node's Serial is native USB-CDC, so its log vanishes the
// instant USB is unplugged - which is exactly when it runs from the
// supercapacitor and exactly when the log matters. The node mirrors every line
// out of GPIO20 as plain UART. This board turns that back into a COM port.
//
// Wiring - two wires, and only two:
//
//   node GPIO20 ---[ 1k ]--- D7 (GPIO13)     one way: node talks, this listens
//   node GND --------------- GND             common reference, mandatory
//
// Do NOT link 3V3 or 5V between the boards. The node runs from the cap, this
// runs from USB; a supply link back-feeds the cap and destroys the very
// measurement the run exists to make. Ground and one signal, nothing else.
//
// Why SoftwareSerial rather than the hardware UART: the ESP8266's only UART
// with an RX is on GPIO1/GPIO3, and those are hardwired to the CH340. Letting
// the node drive GPIO3 would put two drivers on one line. UART1 is TX-only. So
// reception has to be soft, and the hardware Serial stays on USB duty.

// Optional third wire - independent Vcap witness on A0:
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

SoftwareSerial nodeIn;

static uint32_t lastVcap   = 0;
static bool     atLineStart = true;   // never interleave into a node line

static float readVcap() {
  uint32_t acc = 0;
  for (uint8_t i = 0; i < VCAP_AVG; i++) acc += analogRead(A0);
  return (acc / (float)VCAP_AVG) * VCAP_SCALE;
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

  nodeIn.begin(NODE_BAUD, SWSERIAL_8N1, RX_PIN, -1, false, RX_BUF);

  Serial.println();
  Serial.printf("# logger_d1_mini - listening on D7 (GPIO13) at %d 8N1\n", NODE_BAUD);
  Serial.println("# node output passes through verbatim; own readings are tagged 'L,'");
#if VCAP_SENSE
  Serial.printf("# L,t_s,Vcap_V   (A0 via 300k, scale %.6f V/count)\n", VCAP_SCALE);
#endif
}

void loop() {
  while (nodeIn.available()) {
    char c = (char)nodeIn.read();
    Serial.write(c);
    atLineStart = (c == '\n');
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
#endif
}
