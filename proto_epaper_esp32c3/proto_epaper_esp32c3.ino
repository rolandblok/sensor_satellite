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
#include <esp_partition.h>
#include <Preferences.h>

// ---------------- config ----------------
#define PANEL_V2        1      // 1 = Waveshare 2.9" V2 (SSD1680, "V2" on the back)
                               // 0 = original V1 (IL3820)
#define USE_DEEP_SLEEP  1      // 1 = sleep between cycles; drops the USB serial port
#define CYCLE_S         300    // seconds between refreshes - keep >= 180 for e-paper
                               // DEFAULT only: the live value is cfg.cycleS, which
                               // the serial console can change - see "settings"
#define LOG_S           2      // serial log interval when not deep sleeping
#define FW_VERSION      "v1.3" // shown small, bottom right of every frame. Bump it
                               // when reflashing, so a panel photograph says
                               // which build produced it.
#define ALTITUDE_M      17.0f  // Eindhoven, ~17 m AMSL - for sea-level pressure
#define MIN_REFRESH_C   0.0f   // below this the panel is skipped, image is kept

// ---------------- board / pin map ----------------
// Both boards are the same silicon. The XIAO brings out 11 GPIO against the
// SuperMini's 13 - GPIO0 and GPIO1 are not bonded out - so the I2C bus has to
// move. The e-paper pins differ too, but for a mechanical reason, below.
// See gpio_xiao.md.
#define BOARD_XIAO      1      // 0 = ESP32-C3 SuperMini (what this build runs on)
                               // 1 = Seeed XIAO ESP32-C3

// D-numbers are the XIAO's silk. They do not correspond to the GPIO numbers in
// any regular way - D6 is GPIO21 and D7 is GPIO20, adjacent on the chip and on
// opposite sides of the board. Wire from the GPIO number, not the silk.
// The e-paper pins are the same on both boards - this is what is soldered on
// the XIAO as of 2026-09-18, and it matches the SuperMini's wiring.
//
// DC is on GPIO21, not GPIO3: GPIO3 is the only ADC1 channel left for VSENSE.
// GPIO21 is UART0 TX, free here because Serial is USB-CDC on GPIO18/19.
//
// GPIO21 must carry DC and nothing else. The ROM bootloader prints its boot log
// there at every reset and every deep-sleep wake, which is harmless for a line
// the C3 drives and the panel only reads. RST would be hammered with ~8.7 us
// pulses while the panel hibernates - it would still draw correctly and quietly
// cost power, which is the one number this project is chasing. BUSY would be
// worse: the panel drives it, so the ROM would fight the peripheral.
#define EPD_SCK   4            // XIAO D2  CLK
#define EPD_RST   5            // XIAO D3
#define EPD_MOSI  6            // XIAO D4  DIN
#define EPD_CS    7            // XIAO D5
#define EPD_DC    21           // XIAO D6

// BUSY is NOT CONNECTED: the panel's BUSY pad broke off on 2026-09-18.
// -1 makes GxEPD2 fall back on fixed worst-case delays instead of polling, and
// for this panel they are tuned close to the measured times - power_on 100 ms
// against 95.9, full refresh 4100 against 4012, power_off 150 against 140.4.
// A full refresh costs ~4350 ms blind against ~4248 ms polled, so the whole
// price is ~100 ms of extra awake time per 300 s cycle, under 2% of the burst.
//
// Leaving it as GPIO10 with the pad broken would be far worse than this. The
// pin would float, and _busy_level is HIGH with a 10 s _busy_timeout: read HIGH
// and every wait burns the full 10 s - three per refresh, so ~30 s awake
// instead of 4.3 - while read LOW returns instantly and clocks commands into a
// panel that is still busy.
//
// What is given up: a stuck panel can no longer be detected, and the margin on
// the refresh delay is ~2%, which is thin only if the panel runs slow. It does
// that when cold, and this is an indoor sculpture. GPIO10 / D10 is now free.
#define EPD_BUSY  -1           // was GPIO10 / XIAO D10
#define EPD_MISO  -1           // MUST be -1, or SPI claims a pin already in use

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

// The BOOT button, read as an ordinary input. GPIO9 is a strapping pin, but it
// is only *sampled* at the instant reset is released - nothing looks at it
// afterwards - so the ROM's download mode and this are not in conflict: hold it
// during reset and the sketch never runs; hold it after reset and the sketch
// sees it. That is the difference between flashing and reading the log.
//
// Deliberately not added to parkPins(): a latched level on a strapping pin lives
// in the RTC domain and survives a reflash, and a hold stuck LOW here would
// force download mode on every boot and look like a dead board. Whether GPIO9
// floats during deep sleep, and what that costs, is worth a measurement.
#define BOOT_PIN   9       // XIAO D9

#define VSENSE_PIN 3       // XIAO D1 - ADC1_3 on both boards
#define VDIV_NUM   2.0f    // (R3+R4)/R4
#define VDIV_CAL   1.0149f // 2026-08-28: DMM 4.81 V vs 4.7962 V, mean of 5 boots
                           // (spread 4.782-4.811, so this is good to ~0.3%)
                           // 2026-09-25: carries over to the soldered XIAO as
                           // is, within 0.02 V of the DMM - no per-chip retrim.
#define CAP_F      4.0f    // supercap, for turning dV per cycle into a current

// ---------------- low-power hold ----------------
// Hysteresis around the brownout point, and the reason it is needed: 4 F is
// enormous next to this load, so a failed boot costs only ~1.5 mV. The brownout
// reset therefore leaves Vcap exactly where it was, the node retries at once,
// and it loops at ~20 mA - walking the cap down ~5 mV/s until the chip can no
// longer start at all. Every one of those attempts buys zero readings.
// Measured brownout for this configuration is 3.04 V (2026-08-30 cap-only run).
//
// The fix is to spend the cap only when there is enough in it. A full cycle
// costs ~70 mV of the 4 F pack - 9.1 s awake, most of it the 4.3 s refresh. A
// held-off wake costs ~3 mV, because it reads the divider and goes straight
// back to sleep without touching Serial, SPI or the panel.
// These four are DEFAULTS. The live values are in cfg, below, and the serial
// console changes them without a reflash.
#define VCAP_HOLD    3.50f // cycling stops below this
#define VCAP_RESUME  3.80f // and does not restart until this - the hysteresis
#define VCAP_FLOOR   3.25f // below this, do not even spend a refresh saying so
#define HOLD_S       900   // poll interval while held off
#define TREND_MV     20.0f // trend deadband; boot-to-boot ADC spread is +-15 mV

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
RTC_DATA_ATTR float    vcapPrev  = 0.0f;   // last wake's Vcap - the trend needs it
RTC_DATA_ATTR bool     holding   = false;  // the hysteresis latch
RTC_DATA_ATTR uint32_t holdPolls = 0;      // consecutive held-off wakes
RTC_DATA_ATTR uint32_t sleptS    = CYCLE_S;// how long the last sleep was, so the
                                           // trend can be stated as a current
// Flash log bookkeeping. logHead is a cache - it is re-derived from flash on any
// boot that cannot trust it, so losing RTC costs a binary search, not the log.
RTC_DATA_ATTR uint32_t logHead      = 0;   // next free slot, 0 = "ask flash"
RTC_DATA_ATTR uint16_t vSleepPrevMv = 0;   // Vcap at the previous sleep
RTC_DATA_ATTR uint32_t pendSeq      = 0;   // nonzero = a cycle is in flight
RTC_DATA_ATTR uint16_t pendVbootMv  = 0;   // ...and the Vcap it started from
RTC_DATA_ATTR uint8_t  pendFlags    = 0;

// ---------------- settings ----------------
// The power thresholds and both sleep lengths, changeable over the serial
// console without reflashing. Stored in the NVS partition (not the log's
// `spiffs` one), so they survive power loss and reflashing alike; only an
// `--erase-all` or the console's `r` brings back the #define defaults above.
//
// Read from flash on every cold boot and cached in RTC memory, so a timer wake
// - the hot path, and the held-off poll above all - pays nothing for them. A
// change made in the console is a cold boot by definition, so the cache is
// always current.
#define CFG_MAGIC  0x43464731UL      // "CFG1"; bump if the struct changes
struct Config {
  uint32_t magic;
  uint16_t cycleS;     // sleep between updates
  uint16_t holdS;      // sleep between held-off polls
  float    vHold;      // cycling stops below this
  float    vResume;    // and restarts at this
  float    vFloor;     // below this, not even a hold frame
};
RTC_DATA_ATTR Config cfg = {0, 0, 0, 0, 0, 0};

static const Config CFG_DEFAULT = {CFG_MAGIC, CYCLE_S, HOLD_S,
                                   VCAP_HOLD, VCAP_RESUME, VCAP_FLOOR};

static int8_t  sdaPin = -1, sclPin = -1;
static uint8_t bmeAddr = 0;
static bool    bmeOk  = false;

// Must precede the first function definition: the Arduino preprocessor injects
// generated prototypes there, and they reference this type.
struct Reading {
  float tC, rh, hPa, hPaSea, dewC;
  float vcap;
  float dv;          // Vcap now minus Vcap at the previous wake
  bool  haveTrend;   // false on the first wake after a cold boot
};

// Which frame refresh() should paint.
enum FrameKind { FRAME_OK, FRAME_NOSENSOR, FRAME_HOLD };

// ---------------- flash log ----------------
// The panel is bistable and USB is unplugged on cap power, so without this a
// night run leaves exactly one frame and no history. Records go into the
// `spiffs` data partition, which nothing else in this sketch touches, as a plain
// append-only array of fixed 16-byte slots - no filesystem to mount, no metadata
// to rewrite, one 16-byte write per wake.
//
// Slot 0 is a header. Records start at slot 1. Sectors are erased lazily, just
// before the first slot in one is used, so no single operation costs more than
// one 4096-byte erase.
#define LOG_MAGIC    0x53415431UL  // "SAT1" in slot 0, or the partition is virgin
#define LOG_SLOT     16            // bytes per slot; flash writes want 4-byte alignment
#define LOG_PER_SECT (4096 / LOG_SLOT)

// Flags. LF_INCOMPLETE is the important one: it is written on the *next* boot,
// for an attempt that never reached its sleep. That is how a brownout part-way
// through an update leaves a trace instead of a silence - which is the whole
// point of the exercise, since the question is which starting voltages fail.
#define LF_COLD       0x01   // power-on, RESET or brownout - not a timer wake
#define LF_HELD       0x02   // held-off poll; no update attempted
#define LF_BME_OK     0x04
#define LF_REFRESHED  0x08   // the panel was actually redrawn
#define LF_HOLDFRAME  0x10   // the LOW POWER frame was drawn
#define LF_INCOMPLETE 0x20   // this attempt never finished - see above

struct __attribute__((packed)) LogRec {
  uint32_t seq;        // 0xFFFFFFFF in an erased slot; LOG_MAGIC in slot 0
  uint16_t vBootMv;    // Vcap at the top of setup(), before anything powers up
  uint16_t vSleepMv;   // Vcap immediately before deep sleep; 0 if never reached
  int16_t  tCc;        // centi-degrees C; INT16_MIN for no reading
  uint16_t rhD;        // relative humidity in 0.1 %
  uint16_t sleptS;     // the sleep that preceded this boot
  uint8_t  flags;
  uint8_t  bootN;      // low byte of bootCount - spots an RTC loss
};
static_assert(sizeof(LogRec) == LOG_SLOT, "LogRec must stay one slot");

static LogRec gRec;                  // filled through the wake, written at sleep
static const esp_partition_t *gPart = nullptr;


// ---------------- settings, continued ----------------
// Defined down here, after every type the sketch declares: the Arduino
// preprocessor puts its generated prototypes ahead of the first function.
// Returns why a config is unusable, or nullptr. Every stored config passes
// through here, so a corrupt or hand-typed value can never strand the node:
// the worst it gets is the defaults.
static const char *cfgProblem(const Config &c) {
  if (c.magic != CFG_MAGIC)                     return "bad magic";
  if (c.cycleS < 60)                            return "cycle_s below 60";
  if (c.holdS  < 10)                            return "poll_s below 10";
  if (!(c.vFloor >= 2.50f && c.vResume <= 5.00f)) return "voltages outside 2.50-5.00";
  if (!(c.vFloor < c.vHold && c.vHold < c.vResume))
    return "need v_floor < v_hold < v_resume";
  return nullptr;
}

static void cfgLoad() {
  Config c = {};
  Preferences prefs;
  if (prefs.begin("sat", true)) {           // read-only; false if never written
    if (prefs.getBytes("cfg", &c, sizeof c) != sizeof c) c.magic = 0;
    prefs.end();
  }
  cfg = cfgProblem(c) ? CFG_DEFAULT : c;
}

static bool cfgSave(const Config &c) {
  Preferences prefs;
  if (!prefs.begin("sat", false)) return false;
  const bool ok = prefs.putBytes("cfg", &c, sizeof c) == sizeof c;
  prefs.end();
  return ok;
}

// ---------------- logging ----------------
// Everything goes to both ports. Cheap insurance: a line that only reaches USB
// is a line that does not exist during a cap run.
// False until the ports are open. A held-off wake never opens them - it reads
// the divider and sleeps - so every log call on that path has to be free, and
// mark()'s delay(15) especially so.
static bool logUp = false;

static void logBoth(const char *fmt, ...) {
  if (!logUp) return;
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
  if (!logUp) return;
  logBoth("# mark: %s\n", what);
#if USE_LOG_MIRROR
  Serial0.flush();
#endif
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.flush();
#endif
  delay(15);                   // let the last byte clear the shift register
}

// ---------------- flash log ----------------
static const esp_partition_t *logPart() {
  if (!gPart)
    gPart = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                     ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
  return gPart;
}

static uint32_t logSlots() {
  const esp_partition_t *pt = logPart();
  return pt ? pt->size / LOG_SLOT : 0;
}

static bool logReadSlot(uint32_t slot, LogRec *out) {
  const esp_partition_t *pt = logPart();
  if (!pt || slot >= logSlots()) return false;
  return esp_partition_read(pt, slot * LOG_SLOT, out, LOG_SLOT) == ESP_OK;
}

// Erase sector 0 and stamp the header. Only reached on a virgin partition, or on
// one holding something unrecognised - which can only be an old filesystem,
// since nothing else in this sketch writes flash.
static bool logInit() {
  const esp_partition_t *pt = logPart();
  if (!pt) return false;
  if (esp_partition_erase_range(pt, 0, 4096) != ESP_OK) return false;
  LogRec h = {};
  h.seq     = LOG_MAGIC;
  h.vBootMv = LOG_SLOT;
  return esp_partition_write(pt, 0, &h, LOG_SLOT) == ESP_OK;
}

// First slot whose seq is still erased. Binary search, so re-deriving the head
// after an RTC loss costs ~17 reads instead of a scan of the whole partition.
static uint32_t logFindHead() {
  const uint32_t n = logSlots();
  if (n < 2) return 0;
  LogRec r;
  if (!logReadSlot(0, &r)) return 0;
  if (r.seq != LOG_MAGIC) {                                      // virgin, or not ours
    if (!logInit()) return 0;
    return 1;
  }
  if (!logReadSlot(1, &r) || r.seq == 0xFFFFFFFFUL) return 1;    // header only
  uint32_t lo = 1, hi = n;              // lo is used; hi is free or past the end
  while (hi - lo > 1) {
    const uint32_t mid = lo + (hi - lo) / 2;
    if (logReadSlot(mid, &r) && r.seq != 0xFFFFFFFFUL) lo = mid; else hi = mid;
  }
  return lo + 1;
}

static bool logAppend(LogRec &r) {
  const esp_partition_t *pt = logPart();
  if (!pt) return false;
  if (logHead == 0) logHead = logFindHead();
  if (logHead == 0 || logHead >= logSlots()) return false;   // no partition, or full

  // Lazy erase: landing on the first slot of a sector means it is still dirty.
  if (logHead % LOG_PER_SECT == 0 &&
      esp_partition_erase_range(pt, logHead * LOG_SLOT, 4096) != ESP_OK)
    return false;

  r.seq = logHead;
  if (esp_partition_write(pt, logHead * LOG_SLOT, &r, LOG_SLOT) != ESP_OK)
    return false;
  logHead++;
  return true;
}

static void logFlashStatus() {
  const esp_partition_t *pt = logPart();
  if (!pt) { logBoth("# flash log: no spiffs partition - NOT LOGGING\n"); return; }
  if (logHead == 0) logHead = logFindHead();
  const uint32_t n = logSlots();
  logBoth("# flash log: %s, %lu/%lu slots, ~%lu days left at %d s\n",
                pt->label, (unsigned long)logHead, (unsigned long)n,
                (unsigned long)((n - logHead) / (86400UL / cfg.cycleS)), cfg.cycleS);
}

// CSV to whichever port is listening. dv_sleep is what the sleep gained - the
// light signal, with the cycle's own consumption excluded - and dv_cycle is what
// the update cost. Both are derived here rather than stored, so a record stays
// 16 bytes and the arithmetic can be fixed without reflashing.
static void logDump() {
  const uint32_t n = logSlots();
  if (logHead == 0) logHead = logFindHead();
  logBoth("# flash log dump: %lu records\n",
                (unsigned long)(logHead > 1 ? logHead - 1 : 0));
  logBoth("seq,flags,cold,held,incomplete,refreshed,bootN,slept_s,"
          "v_boot_mV,v_sleep_mV,dv_sleep_mV,dv_cycle_mV,net_uA,T_C,RH_pct\n");
  uint16_t prevSleep = 0;
  LogRec r;
  for (uint32_t i = 1; i < logHead && i < n; i++) {
    if (!logReadSlot(i, &r) || r.seq == 0xFFFFFFFFUL) break;
    const int32_t dvSleep = prevSleep ? (int32_t)r.vBootMv - (int32_t)prevSleep : 0;
    const int32_t dvCycle = r.vSleepMv ? (int32_t)r.vSleepMv - (int32_t)r.vBootMv : 0;
    const float   netUA   = (prevSleep && r.sleptS)
                          ? CAP_F * (dvSleep / 1000.0f) / (float)r.sleptS * 1e6f : 0.0f;
    logBoth("%lu,0x%02X,%d,%d,%d,%d,%u,%u,%u,%u,%+ld,%+ld,%+.0f,",
                  (unsigned long)r.seq, r.flags,
                  (r.flags & LF_COLD) ? 1 : 0, (r.flags & LF_HELD) ? 1 : 0,
                  (r.flags & LF_INCOMPLETE) ? 1 : 0, (r.flags & LF_REFRESHED) ? 1 : 0,
                  r.bootN, r.sleptS, r.vBootMv, r.vSleepMv,
                  (long)dvSleep, (long)dvCycle, netUA);
    if (r.tCc == INT16_MIN) logBoth(",\n");
    else                    logBoth("%.2f,%.1f\n", r.tCc / 100.0f, r.rhD / 10.0f);
    if (r.vSleepMv) prevSleep = r.vSleepMv;   // an incomplete attempt breaks the chain
  }
  logBoth("# end of dump\n");
}

static void logErase() {
  const esp_partition_t *pt = logPart();
  if (!pt) return;
  logBoth("# erasing %lu bytes, this takes a moment...\n", (unsigned long)pt->size);
  Serial.flush();
  if (esp_partition_erase_range(pt, 0, pt->size) == ESP_OK && logInit()) {
    logHead      = 1;
    vSleepPrevMv = 0;
    logBoth("# flash log erased\n");
  } else {
    logBoth("# ERASE FAILED\n");
  }
}

// Command window, offered only when a USB host actually has the port open - so a
// cap-powered wake with nothing listening pays nothing for it. Opening the port
// resets the C3, so by the time this runs the terminal is already there and the
// character can be sent straight away.
// True once somebody is listening. Two independent triggers, because one is not
// enough: `Serial` only goes true when the terminal asserts DTR, which not every
// terminal does, so any received byte counts as well - meaning you can always
// force the window by mashing a key while it boots. Only ever called on a cold
// boot, so a cap-powered timer wake never pays for the wait.
static bool hostPresent() {
  for (int i = 0; i < 40; i++) {          // up to ~2 s
    if (Serial || Serial.available()) return true;
    delay(50);
  }
  return false;
}

static void cfgPrint() {
  logBoth("# settings (flash):  cycle_s %u  poll_s %u  v_hold %.2f  v_resume %.2f"
          "  v_floor %.2f\n", cfg.cycleS, cfg.holdS, cfg.vHold, cfg.vResume, cfg.vFloor);
  logBoth("# defaults:          cycle_s %u  poll_s %u  v_hold %.2f  v_resume %.2f"
          "  v_floor %.2f\n", CYCLE_S, HOLD_S, VCAP_HOLD, VCAP_RESUME, VCAP_FLOOR);
}

// The rest of an "s <name> <value>" line, typed after the 's'. Echoes, so a
// terminal without local echo still shows what was typed.
static bool readLine(char *buf, size_t len, uint32_t timeoutMs) {
  size_t n = 0;
  const uint32_t until = millis() + timeoutMs;
  while ((int32_t)(millis() - until) < 0) {
    if (!Serial.available()) { delay(5); continue; }
    const int c = Serial.read();
    if (c == '\r' || c == '\n') { buf[n] = 0; logBoth("\n"); return true; }
    if (n + 1 < len) { buf[n++] = (char)c; Serial.write((char)c); }
  }
  buf[n] = 0;
  return false;
}

static void cfgSet() {
  char line[48];
  if (!readLine(line, sizeof line, 20000)) { logBoth("\n# set: timed out\n"); return; }
  char name[16];
  float v;
  if (sscanf(line, " %15s %f", name, &v) != 2) {
    logBoth("# set: use  s <name> <value>   names: cycle_s poll_s v_hold v_resume v_floor\n");
    return;
  }
  Config c = cfg;
  if      (!strcmp(name, "cycle_s"))  c.cycleS  = (uint16_t)constrain(v, 0, 65535);
  else if (!strcmp(name, "poll_s"))   c.holdS   = (uint16_t)constrain(v, 0, 65535);
  else if (!strcmp(name, "v_hold"))   c.vHold   = v;
  else if (!strcmp(name, "v_resume")) c.vResume = v;
  else if (!strcmp(name, "v_floor"))  c.vFloor  = v;
  else { logBoth("# set: unknown name '%s'\n", name); return; }
  if (const char *why = cfgProblem(c)) { logBoth("# set: refused - %s\n", why); return; }
  if (!cfgSave(c)) { logBoth("# set: FLASH WRITE FAILED, nothing changed\n"); return; }
  cfg = c;
  if (cfg.cycleS < 180)
    logBoth("# note: cycle_s below 180 refreshes the e-paper more often than it is rated for\n");
  cfgPrint();
}

// Returns true if it did something, so the caller can offer the window again -
// dump, look, erase, dump again, without a reset between each.
static bool serveCommands() {
  logBoth("# d = dump log, e = erase log, p = settings, s <name> <value> = set,"
          " r = restore defaults, c = continue (cycle then sleep). Waiting 5 s...\n");
  const uint32_t until = millis() + 5000;
  while ((int32_t)(millis() - until) < 0) {
    if (!Serial.available()) { delay(20); continue; }
    const int c = Serial.read();
    if (c == 'd') { logDump();  return true; }
    if (c == 'e') { logErase(); return true; }
    if (c == 'p') { cfgPrint(); return true; }
    if (c == 's') { Serial.write('s'); cfgSet(); return true; }
    if (c == 'r') {
      if (cfgSave(CFG_DEFAULT)) { cfg = CFG_DEFAULT; logBoth("# defaults restored\n"); }
      else                      logBoth("# FLASH WRITE FAILED\n");
      cfgPrint();
      return true;
    }
    if (c == 'c') return false;
  }
  logBoth("# no command, continuing\n");
  return false;
}

// Held BOOT means "do not sleep, I want to read the log". Stays here forever,
// so there is no window to miss and no race to win - the way out is RESET.
// Reached even when the hold gate would otherwise have slept immediately, which
// is the point: a flat cap is exactly when you most want to read the log.
static void consoleMode(const Reading &sense) {
  logBoth("\n# BOOT held - console mode, this board will not sleep.\n");
  logBoth("# Vcap %.3f V, boot #%lu\n", sense.vcap, (unsigned long)bootCount);
  logFlashStatus();
  cfgPrint();
  logBoth("# release BOOT now; press RESET to leave console mode.\n");
  for (;;) {
    serveCommands();
    delay(50);
  }
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

// Trend marker. The GFX fonts here are ASCII only - no arrow glyphs - so the
// triangle is drawn. Inside the deadband it is a bar: a 4 F cap moves ~70 mV per
// cycle when the node is losing ground, so +-20 mV of ADC spread is genuinely
// "flat" and an arrow that flickered on noise would teach the wrong thing.
static void drawTrend(int16_t x, int16_t yBase, float dv) {
  const float mv = dv * 1000.0f;
  if (mv >  TREND_MV)      display.fillTriangle(x, yBase, x + 10, yBase, x + 5, yBase - 9, GxEPD_BLACK);
  else if (mv < -TREND_MV) display.fillTriangle(x, yBase - 9, x + 10, yBase - 9, x + 5, yBase, GxEPD_BLACK);
  else                     display.fillRect(x, yBase - 6, 10, 2, GxEPD_BLACK);
}

// Small print: the built-in 5x7 font at size 1, half the height of the 9 pt
// lines. Unlike every GFX font here it is positioned by its TOP-left corner
// rather than its baseline, and each glyph is 6 px wide including the gap.
#define FW_VER_W ((int16_t)(6 * (sizeof(FW_VERSION) - 1)))

// Right-aligns small text to xRight with its bottom on baseline y; returns
// where it starts, so the next item can be placed to its left.
static int16_t drawSmallRight(const char *s, int16_t xRight, int16_t y) {
  const int16_t x = xRight - (int16_t)(6 * strlen(s) - 1);
  display.setFont(NULL);
  display.setTextSize(1);
  display.setCursor(x, y - 7);
  display.print(s);
  display.setFont(&FreeSans9pt7b);      // restore for whatever draws next
  return x;
}

// Header is shared by every frame: what this is, Vcap, the charge trend and the
// boot counter. Laid out right to left. The trend is what the sleep gained - the
// whole "is there enough light" readout. The marker carries the direction, so
// the number is printed unsigned; a signed one next to the flat bar read as a
// double minus.
static void drawHeader(const Reading &r) {
  char buf[16];
  const int16_t y = 15;
  display.setFont(&FreeSans9pt7b);
  display.setCursor(4, y);
  display.print("sensor satellite");

  snprintf(buf, sizeof(buf), "#%lu", (unsigned long)bootCount);
  int16_t x = drawSmallRight(buf, display.width() - 4, y);
  if (r.haveTrend) {
    snprintf(buf, sizeof(buf), "%.0f mV", fabsf(r.dv * 1000.0f));
    x = drawSmallRight(buf, x - 8, y);
    x -= 14;
    drawTrend(x, y, r.dv);
  }
  snprintf(buf, sizeof(buf), "%.2f V", r.vcap);
  drawRight(buf, x - 6, y);
  display.drawFastHLine(0, 21, display.width(), GxEPD_BLACK);
}

// Firmware version, bottom right corner, in the same small print.
static void drawVersion() {
  display.setFont(NULL);
  display.setTextSize(1);
  display.setCursor(display.width() - 2 - FW_VER_W, display.height() - 9);
  display.print(FW_VERSION);
  display.setFont(&FreeSans9pt7b);      // restore for whatever draws next
}

static void drawFrame(bool ok, const Reading &r) {
  char buf[32];
  const int16_t W = display.width();     // 296 in landscape
  const int16_t H = display.height();    // 128

  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);

  drawHeader(r);

  if (!ok) {
    display.setFont(&FreeSansBold9pt7b);
    display.setCursor(4, 60);
    display.print("NO SENSOR");
    display.setFont(&FreeSans9pt7b);
    display.setCursor(4, 84);
    display.print("BME280 not on I2C");
    display.setCursor(4, 104);
    display.print("check CSB->3V3, SDO->GND, power");
    drawVersion();
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
  drawVersion();
}

// The frame the node leaves on the panel while it waits for light. It is drawn
// once, on entry to the hold, and the panel keeps it with no power - so the
// sculpture explains its own silence for as long as the silence lasts.
static void drawHoldFrame(const Reading &r) {
  char buf[40];
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  drawHeader(r);

  display.setFont(&FreeSansBold9pt7b);
  display.setCursor(4, 60);
  display.print("Low Power - waiting for light");

  display.setFont(&FreeSans9pt7b);
  display.setCursor(4, 86);
  snprintf(buf, sizeof(buf), "cycling resumes at %.2f V", cfg.vResume);
  display.print(buf);
  drawVersion();
}

// Full refresh. At CYCLE_S >= 180 s this is within spec and avoids the
// ghosting bookkeeping that partial updates need.
static void refresh(FrameKind kind, const Reading &r) {
  mark("refresh entered");
  display.setFullWindow();
  display.firstPage();
  do {
    if (kind == FRAME_HOLD) drawHoldFrame(r);
    else                    drawFrame(kind == FRAME_OK, r);
  } while (display.nextPage());
  display.hibernate();      // drop the panel's HV rails - required between updates
}

// ---------------- logging ----------------
static void logHeader() {
  logBoth("# t_s,T_C,RH_pct,dew_C,P_station_hPa,P_sea_hPa,Tmin_C,Tmax_C,"
          "Vcap_V,dVcap_mV,net_uA\n");
}

// net_uA is the honest form of the trend: I = C dV/dt across the sleep that
// just ended, so it is a real harvest-minus-consumption figure, and it stays
// correct when the last sleep was a HOLD_S poll rather than a CYCLE_S cycle.
static float netCurrentUA(const Reading &r) {
  if (!r.haveTrend || sleptS == 0) return 0.0f;
  return CAP_F * r.dv / (float)sleptS * 1e6f;
}

static void logReading(const Reading &r) {
  logBoth("%.1f,%.2f,%.1f,%.1f,%.2f,%.2f,%.1f,%.1f,%.3f,%+.0f,%+.0f\n",
                millis() / 1000.0f,
                r.tC, r.rh, r.dewC, r.hPa, r.hPaSea, tMin, tMax, r.vcap,
                r.haveTrend ? r.dv * 1000.0f : 0.0f, netCurrentUA(r));
}

// Vcap is passed in rather than read here: setup() reads it first thing,
// because whether this runs at all depends on it.
static void runCycle(const Reading &sense) {
  Reading r = sense;
  if (!bmeRead(r)) {
    logBoth("# BME280 read failed - showing fault frame (Vcap %.3f V)\n", r.vcap);
    refresh(FRAME_NOSENSOR, r);
    return;
  }

  if (r.tC < tMin) tMin = r.tC;
  if (r.tC > tMax) tMax = r.tC;
  gRec.flags |= LF_BME_OK;
  gRec.tCc    = (int16_t)(r.tC * 100.0f + (r.tC < 0 ? -0.5f : 0.5f));
  gRec.rhD    = (uint16_t)(r.rh * 10.0f + 0.5f);
  logReading(r);

  // E-paper refresh is unreliable below freezing. The panel is bistable, so
  // keeping the previous image costs nothing but staleness.
  if (r.tC < MIN_REFRESH_C) {
    logBoth("# %.1f C below %.1f C - skipping refresh, keeping last image\n",
                  r.tC, MIN_REFRESH_C);
    return;
  }
  refresh(FRAME_OK, r);
  gRec.flags |= LF_REFRESHED;
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
#if EPD_BUSY >= 0
                             (gpio_num_t)EPD_BUSY,
#endif
                             (gpio_num_t)10};   // ex-BUSY, free but may hold a latch
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
  // pin carries only an external 10k pull-up to 3V3, which draws nothing while
  // the pad is high-Z - driving it LOW would cost ~330 uA, driving it HIGH saves
  // nothing. The same "leave it alone" applies.
  //
  // An earlier comment here called it a blue LED with 206 uA of drive current,
  // from the 2026-09-03 shunt session that was later thrown out for a ground
  // loop. That 206 uA is unexplained, not an LED. See gpio_xiao.md.
#if !BOARD_XIAO
  pinMode(2,  INPUT_PULLUP);                   // strapping, unconnected by design
#endif                                         // on the XIAO GPIO2 *is* FORCE_SCL
  pinMode(FORCE_SDA, INPUT_PULLUP);            // I2C idles high
  pinMode(FORCE_SCL, INPUT_PULLUP);            // XIAO: this is GPIO2, strapping
#if EPD_BUSY >= 0
  pinMode(EPD_BUSY,  INPUT_PULLDOWN);          // BUSY idles low
#else
  pinMode(10, INPUT_PULLDOWN);                 // ex-BUSY, unconnected - do not float
#endif

  // A pull set by pinMode alone does not survive deep sleep - the digital
  // domain powers down. Latching is what makes the pull mean anything here.
#if !BOARD_XIAO
  gpio_hold_en((gpio_num_t)2);
#endif
  gpio_hold_en((gpio_num_t)FORCE_SDA);
  gpio_hold_en((gpio_num_t)FORCE_SCL);
#if EPD_BUSY >= 0
  gpio_hold_en((gpio_num_t)EPD_BUSY);
#else
  gpio_hold_en((gpio_num_t)10);
#endif
  gpio_deep_sleep_hold_en();
#endif
}

// Park the pins and sleep. Does not return. Every exit from setup() comes
// through here, so nothing can sleep with the pads left floating - and nothing
// can sleep without its record reaching flash.
static void sleepFor(uint32_t seconds) {
  // Vcap one last time, as late as possible: paired with vBootMv this is what
  // the wake actually cost, and it is the denominator for everything the log is
  // meant to answer.
  gRec.vSleepMv = (uint16_t)(readVcap() * 1000.0f + 0.5f);
  if (!logAppend(gRec))
    logBoth("# flash log append FAILED - full, or no partition\n");
  vSleepPrevMv = gRec.vSleepMv;
  pendSeq      = 0;            // this attempt finished; nothing to report next boot

  mark("parkPins enter");
  parkPins();
  mark("parkPins returned - sleeping now");
  sleptS = seconds;
  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  esp_deep_sleep_start();      // does not return; setup() runs again on wake
}

void setup() {
  unparkPins();

  // Settings from flash on a cold boot; a timer wake trusts the RTC copy. The
  // magic check also catches an RTC that came up blank for any other reason.
  if (cfg.magic != CFG_MAGIC ||
      esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER)
    cfgLoad();

  // Vcap before anything else is powered up. A held-off wake has to be cheap to
  // be worth making, and everything below this line - Serial, I2C, SPI, the
  // panel - is what makes a wake expensive. Reading the divider needs none of
  // it. RTC memory carries the previous reading, so the trend costs nothing.
  Reading sense = {};
  sense.vcap      = readVcap();
  sense.haveTrend = (vSleepPrevMv > 0);
  // The trend the panel shows is the sleep delta, not wake-to-wake: it is Vcap
  // now against Vcap when the last cycle finished, so it is harvest alone with
  // this node's own consumption excluded. That is the number that answers
  // "is there enough light".
  sense.dv        = sense.haveTrend
                  ? sense.vcap - vSleepPrevMv / 1000.0f : 0.0f;
  vcapPrev        = sense.vcap;

  // Counted here rather than after Serial comes up, because a held-off wake
  // never gets that far and would otherwise be invisible. So this is every
  // wake, and LF_HELD is what separates a poll from an update.
  bootCount++;

  // Read before the hold gate, because the gate's whole job is to sleep without
  // bringing Serial up - and a flat cap is precisely when the log is worth
  // reading. Held BOOT overrides the gate.
  pinMode(BOOT_PIN, INPUT_PULLUP);
  delayMicroseconds(200);              // let the pull-up win against stray charge
  const bool bootHeld = (digitalRead(BOOT_PIN) == LOW);

  gRec          = LogRec();
  gRec.vBootMv  = (uint16_t)(sense.vcap * 1000.0f + 0.5f);
  gRec.tCc      = INT16_MIN;
  gRec.sleptS   = (uint16_t)sleptS;
  gRec.bootN    = (uint8_t)bootCount;

#if USE_DEEP_SLEEP
  // A cold boot is a human event - power applied, or RESET pressed - so someone
  // is watching and wants a frame and a log line. A timer wake is the machine
  // loop, where thrift is the point.
  const bool coldBoot =
      (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER);
  if (coldBoot) holding = false;
  if (coldBoot) gRec.flags |= LF_COLD;

  // USB host on the other end - the board is running off USB, not the cap, so
  // there is nothing to protect and the gates below must not fire. Without this
  // a USB plug-in with the cap low reads Vcap through the divider, which is on
  // the cap side of the jumper, sleeps at the floor before Serial is up, and
  // then holds forever: no port, no frame, a board that looks dead on USB.
  // 2026-10-03. Detected by SOF packets, so a USB power bank does not count.
  // Cold boots only - the host takes ~100 ms to start sending SOFs, which a
  // timer wake should not pay.
  bool usbHost = false;
  if (coldBoot) {
    for (int i = 0; i < 50 && !usbHost; i++) {   // up to ~500 ms
      usbHost = HWCDC::isPlugged();
      if (!usbHost) delay(10);
    }
  }

  // An attempt was in flight when the lights went out. Record it before doing
  // anything else, because this is the datum the whole log exists for: a boot
  // voltage that was not enough to finish an update.
  if (pendSeq != 0) {
    LogRec dead  = LogRec();
    dead.vBootMv = pendVbootMv;
    dead.vSleepMv = 0;                 // never got there
    dead.tCc     = INT16_MIN;
    dead.flags   = pendFlags | LF_INCOMPLETE;
    dead.bootN   = (uint8_t)bootCount;
    logAppend(dead);
    pendSeq = 0;
  }

  // BOOT held beats every power consideration below: USB is plugged in if
  // somebody is pressing buttons, so there is nothing to conserve.
  if (bootHeld) {
    Serial.begin(115200);
    delay(300);
    logUp = true;
    consoleMode(sense);                // never returns
  }

  // The anti-thrash guard, and the only branch that runs below the brownout
  // point: no Serial, no SPI, no panel, whatever woke us. A 4.3 s refresh here
  // costs ~20 mV, which is exactly what a cap this low cannot spare - and it
  // catches the repeated-brownout case, where every reset looks like a cold
  // boot and would otherwise buy another full cycle.
  if (sense.vcap < cfg.vFloor && !usbHost) {
    holding      = true;
    holdPolls++;
    gRec.flags  |= LF_HELD;
    sleepFor(cfg.holdS);       // logs on the way out, like every other exit
  }

  // Hysteresis. Held off, it takes VCAP_RESUME to start cycling again; running,
  // it takes a drop below VCAP_HOLD to stop. The gap is what stops the node
  // oscillating across a single threshold.
  bool enterHold = false;
  if (!coldBoot) {
    if (holding) {
      if (sense.vcap < cfg.vResume) {
        holdPolls++;
        gRec.flags |= LF_HELD;
        sleepFor(cfg.holdS);   // still waiting - cheapest possible wake
      }
      holding = false;         // recovered
    } else if (sense.vcap < cfg.vHold) {
      holding   = true;
      enterHold = true;        // say so on the panel once, then hold
    }
  }
#endif

  Serial.begin(115200);
#if USE_LOG_MIRROR
  // UART0 TX remapped to GPIO20. Must come before display.init(): UART0's
  // default TX is GPIO21, and init()'s pinMode() on DC is what takes GPIO21
  // back off the UART matrix afterwards.
  Serial0.begin(LOG_BAUD, SERIAL_8N1, -1, LOG_TX_PIN);
#endif
  logUp = true;
  delay(300);                  // let USB-CDC enumerate before the first print
  logBoth("\n# proto_epaper_esp32c3 " FW_VERSION "  boot #%lu\n",
                (unsigned long)bootCount);
  cfgPrint();
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
                VSENSE_PIN, VDIV_NUM, VDIV_CAL, sense.vcap);
#if USE_DEEP_SLEEP
  if (sense.haveTrend)
    logBoth("# trend: %+.0f mV over the last %lu s -> net %+.0f uA\n",
                  sense.dv * 1000.0f, (unsigned long)sleptS,
                  netCurrentUA(sense));
  logBoth("# hold gate: %.2f V, stop < %.2f, resume >= %.2f, floor %.2f, "
          "%lu polls held\n",
                sense.vcap, cfg.vHold, cfg.vResume, cfg.vFloor,
                (unsigned long)holdPolls);
  logFlashStatus();

  // Only a cold boot offers the console: a timer wake has nobody listening.
  if (coldBoot && hostPresent())
    while (serveCommands()) { }        // keep offering until told to continue

  if (enterHold) {
    logBoth("# below %.2f V - one frame, then holding, %d s per poll\n",
                  cfg.vHold, cfg.holdS);
    gRec.flags |= LF_HELD | LF_HOLDFRAME;
    refresh(FRAME_HOLD, sense);
    Serial.flush();
    sleepFor(cfg.holdS);
  }
  holdPolls = 0;
#endif

  logHeader();

#if USE_DEEP_SLEEP
  // From here on the expensive part runs. Leave a breadcrumb in RTC so that if
  // the rail collapses mid-update, the next boot can log that it happened.
  pendSeq     = 1;
  pendVbootMv = gRec.vBootMv;
  pendFlags   = gRec.flags;

  mark("runCycle enter");
  runCycle(sense);
  logBoth("# sleeping %u s\n", cfg.cycleS);
  Serial.flush();
#if USE_LOG_MIRROR
  Serial0.flush();
#endif
  // RTC memory works. If a serial capture ever shows "boot #1" on every wake,
  // that is the capture: opening the USB-CDC port resets the chip even with DTR
  // and RTS deasserted, so each read is a cold boot. Check the counter on the
  // panel instead. Measured 2026-09-25.

  sleepFor(cfg.cycleS);
#endif
}

void loop() {
#if !USE_DEEP_SLEEP
  // No hold gate on this path: without deep sleep the board is on USB at a
  // bench, and there is no cap to protect.
  Reading sense = {};
  sense.vcap      = readVcap();
  sense.haveTrend = (vcapPrev > 0.1f);
  sense.dv        = sense.haveTrend ? sense.vcap - vcapPrev : 0.0f;
  vcapPrev        = sense.vcap;
  sleptS          = cfg.cycleS;
  runCycle(sense);
  delay((uint32_t)cfg.cycleS * 1000UL);
#endif
}
