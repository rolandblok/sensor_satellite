// vsense_probe - why does VSENSE read wrong?
// Seeed XIAO ESP32-C3, no external LDO: VCAP feeds the 5V pin and the onboard
// regulator. Nothing else here - no BME280, no e-paper, no deep sleep - so the
// only thing running is the ADC on GPIO3 (XIAO D1).
//
//   VCAP --[ 1M ]--+-- GPIO3 (D1)        tap = VCAP / 2
//                  |
//                [100 nF]
//                  |
//   GND -- [ 1M ]--+
//
// FQBN: esp32:esp32:XIAO_ESP32C3:CDCOnBoot=default   (default = CDC *enabled*)
//
// What this separates, which the node firmware cannot:
//
//  1. Hard saturation. raw == 4095 means the pin is above the ADC's top, not
//     that Vcap is high. An open bottom leg puts the full VCAP on the pin and
//     looks exactly like this.
//  2. Soft compression. 12 dB attenuation is calibrated to ~2.5 V at the pin.
//     The tap reaches that at Vcap 5.0 V - which is where a USB-fed build sits,
//     since the 5V pin is VBUS. The curve flattens there and reads LOW.
//     Watch mv12 stop tracking while the supply still climbs.
//  3. Source impedance / continuity. The pulldown probe switches the internal
//     ~45k pulldown on and re-reads. A real 1M/1M tap is a 500k source and must
//     collapse to roughly a tenth. If it barely moves, the pin is not on the
//     divider it is supposed to be on.
//  4. Gain vs offset. Sweep the supply and fit vcap against the meter. Gain
//     wrong by a few percent is resistor tolerance or a stale VDIV_CAL; a
//     constant shift is ADC offset; a knee near the top is (2).
//
// Beware the meter, too: 10 Mohm across the tap turns the bottom leg into
// 1M || 10M = 909k and the tap reads 4.8% low. Probe VCAP and halve it, or
// expect that error. Do not calibrate against a probe on the tap.

#define VSENSE_PIN 3
#define VDIV_NUM   2.0f
#define VDIV_CAL   1.0149f    // from the SuperMini, 2026-08-28 - suspect here:
                              // efuse ADC calibration is per chip
#define N_AVG      32

static const adc_attenuation_t ATTENS[] = {ADC_0db, ADC_2_5db, ADC_6db, ADC_11db};
static const char *ATTEN_NAMES[]        = {"0dB", "2.5dB", "6dB", "12dB"};

// mean raw counts and mean millivolts at one attenuation, first read discarded
static void readAt(adc_attenuation_t att, float *rawOut, float *mvOut,
                   uint32_t *rawMin, uint32_t *rawMax) {
  analogSetPinAttenuation(VSENSE_PIN, att);
  (void)analogRead(VSENSE_PIN);
  (void)analogReadMilliVolts(VSENSE_PIN);
  uint32_t rAcc = 0, mAcc = 0, rMin = 4095, rMax = 0;
  for (int i = 0; i < N_AVG; i++) {
    uint32_t r = analogRead(VSENSE_PIN);
    rAcc += r;
    if (r < rMin) rMin = r;
    if (r > rMax) rMax = r;
    mAcc += analogReadMilliVolts(VSENSE_PIN);
  }
  *rawOut = rAcc / (float)N_AVG;
  *mvOut  = mAcc / (float)N_AVG;
  *rawMin = rMin;
  *rawMax = rMax;
}

// Sweep all four ranges. Only one is in range at a time, which is the point:
// the ranges must agree where they overlap, and a railed range is a bound.
static void sweepAttenuations() {
  Serial.println("# attenuation sweep - raw 4095 = railed, ranges must agree where they overlap");
  Serial.println("# atten   raw_mean  raw_min  raw_max   mv_at_pin   implied_vcap");
  for (size_t i = 0; i < sizeof(ATTENS) / sizeof(ATTENS[0]); i++) {
    float raw, mv; uint32_t lo, hi;
    readAt(ATTENS[i], &raw, &mv, &lo, &hi);
    Serial.printf("# %-6s  %8.1f  %7lu  %7lu   %7.1f mV   %6.3f V%s\n",
                  ATTEN_NAMES[i], raw, (unsigned long)lo, (unsigned long)hi,
                  mv, mv * VDIV_NUM / 1000.0f,
                  raw > 4094.0f ? "   RAILED - bound only" : "");
  }
}

// Load the tap with the internal pulldown. Source impedance, in situ.
static void pulldownProbe() {
  float rawHiZ, mvHiZ, rawPd, mvPd; uint32_t lo, hi;
  readAt(ADC_11db, &rawHiZ, &mvHiZ, &lo, &hi);

  pinMode(VSENSE_PIN, INPUT_PULLDOWN);
  delay(50);                                    // 500k * 100nF is 50 ms
  readAt(ADC_11db, &rawPd, &mvPd, &lo, &hi);
  pinMode(VSENSE_PIN, INPUT);                   // release
  delay(300);                                   // let the 100 nF recover

  float ratio = (mvHiZ > 1.0f) ? mvPd / mvHiZ : 0.0f;
  Serial.printf("# pulldown probe: %.1f mV hi-Z -> %.1f mV loaded, ratio %.3f\n",
                mvHiZ, mvPd, ratio);
  // ~45k against a 500k source is 0.08; against a stiff node it stays near 1.
  if (ratio < 0.25f)
    Serial.println("#   high impedance, as a 1M/1M tap should be");
  else if (ratio > 0.7f)
    Serial.println("#   LOW impedance - GPIO3 is not on the 1M/1M tap "
                   "(shorted, wrong node, or a much smaller divider)");
  else
    Serial.println("#   in between - check the resistor values actually fitted");

  float rSrc = (ratio > 0.0f && ratio < 1.0f) ? 45000.0f * (1.0f / ratio - 1.0f) : 0.0f;
  Serial.printf("#   implies a source impedance near %.0f kohm "
                "(1M/1M -> 500k; internal pulldown taken as 45k, spec 10-80k)\n",
                rSrc / 1000.0f);
}

void setup() {
  Serial.begin(115200);
  delay(600);                  // USB-CDC enumeration
  Serial.println("\n# vsense_probe - GPIO3 / XIAO D1");
  Serial.printf("# divider x%.2f, cal %.4f, %d samples per reading\n",
                VDIV_NUM, VDIV_CAL, N_AVG);
  Serial.println("# NOTE: opening this port resets the C3, so this is always a cold boot");
  sweepAttenuations();
  pulldownProbe();
  Serial.println("#");
  Serial.println("# steady log follows. Sweep the supply and record the meter at VCAP.");
  Serial.println("# keys: s = attenuation sweep, p = pulldown probe");
  Serial.println("t_ms,raw12,mv12,vcap_raw_V,vcap_cal_V");
}

void loop() {
  if (Serial.available()) {
    int c = Serial.read();
    if (c == 's') sweepAttenuations();
    if (c == 'p') pulldownProbe();
  }

  float raw, mv; uint32_t lo, hi;
  readAt(ADC_11db, &raw, &mv, &lo, &hi);
  // vcap_raw is the uncalibrated ÷2; vcap_cal is what the node firmware reports.
  Serial.printf("%lu,%.1f,%.1f,%.4f,%.4f\n", (unsigned long)millis(), raw, mv,
                mv * VDIV_NUM / 1000.0f, mv * VDIV_NUM * VDIV_CAL / 1000.0f);
  delay(1000);
}
