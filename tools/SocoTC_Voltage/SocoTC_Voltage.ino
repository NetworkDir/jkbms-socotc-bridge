/*
  SocoTC_Voltage - Super Soco TC battery emulator that measures the pack voltage
  Board: Arduino Nano + MAX485. No BMS link needed.

  Measures the pack voltage on A0 through a 270k / 10k divider and answers the
  ECU's battery poll (C5 5C 5A AA ...) with that voltage and an SOC estimated
  from it. If the reading is invalid (divider not connected) it sends
  83 V / 100 % so the bike still runs.

  Wiring:
    MAX485 RO      -> D8
    MAX485 DI      -> D9
    MAX485 DE + RE -> D2   (bridged)
    MAX485 VCC/GND -> Nano 5V / GND
    MAX485 A / B   -> Soco RS485 A / B

    Pack + ──[ 270k ]──┬──── A0
                       │
                    [ 10k ]      optional: 100 nF from A0 to GND
                       │
    Pack − ────────────┴──── Nano GND  (scooter GND)

    Divider ratio (270k + 10k) / 10k = 28  ->  84 V on the pack = 3.0 V on A0.
    With a 5 V reference the maximum is ~140 V.

  Calibration: measure the Nano's 5V pin (powered the way it will be on the bike)
  and put it in VREF_V. Then compare "Pack" in the Serial Monitor with a
  multimeter on the pack and fine-tune VOLTAGE_CAL.

  Required library: AltSoftSerial (Paul Stoffregen)
  Serial Monitor (115200): one status line per second.
*/

#include <AltSoftSerial.h>

// ------------------------- SETTINGS -------------------------
#define PIN_VSENSE        A0       // divider output
#define R_TOP_K           270.0    // kOhm, pack + side
#define R_BOTTOM_K        10.0     // kOhm, GND side
#define VREF_V            5.00     // measured voltage on the Nano 5V pin
#define VOLTAGE_CAL       1.000    // fine trim against a multimeter

#define CELLS_S           20       // cells in series (20S Li-ion = 84 V full)
#define CELL_FULL_MV      4030     // cell voltage reported as 100 % (4030 = JK showing 96 % at 79.9 V)
#define CELL_EMPTY_MV     3000     // cell voltage reported as 0 %
#define MIN_VALID_V       30.0     // readings below this are treated as invalid
#define FALLBACK_V        83       // sent while the reading is invalid
#define FALLBACK_SOC      100

#define TEMP_C            25       // temperature sent (C)
#define VOLT_FILTER_MS    2000.0   // smoothing for the reported voltage
#define SOC_FILTER_MS     30000.0  // slower smoothing for SOC (the voltage sags under load)

#define PIN_DIR           2        // MAX485 DE + RE
#define PIN_LED           13
#define RESPONSE_DELAY_US 1000UL   // delay between the poll and the answer
// ------------------------------------------------------------

// Li-ion (NMC) curve shape over 3.0..4.2 V per cell, stretched onto
// CELL_EMPTY_MV..CELL_FULL_MV. Replace it for LiFePO4.
static const uint16_t SOC_MV[]  = { 3000, 3400, 3550, 3650, 3700, 3740, 3790, 3850, 3920, 4000, 4100, 4200 };
static const uint8_t  SOC_PCT[] = {    0,    5,   10,   20,   30,   40,   50,   60,   70,   80,   90,  100 };

static const float    DIVIDER         = (R_TOP_K + R_BOTTOM_K) / R_BOTTOM_K;
static const uint8_t  SAMPLE_EVERY_MS = 10;
static const uint8_t  SAMPLES_PER_AVG = 16;   // -> a new averaged reading every 160 ms

AltSoftSerial bus;           // RX = D8, TX = D9 (fixed)

// Super Soco telegram: T0 T1 DEST SRC LEN DATA... CHECKSUM 0D
enum { ST_T0, ST_T1, ST_DEST, ST_SRC, ST_LEN, ST_DATA, ST_CHK, ST_END };
static uint8_t  st = ST_T0;
static uint8_t  t0, dest, src, len, idx, chk, calc;
static uint32_t lastByteMs = 0;

static uint8_t  frame[17];
static bool     answerPending = false;
static uint32_t answerAtUs = 0;
static uint32_t lastAnswerMs = 0;

// voltage measurement
static uint32_t adcSum = 0;
static uint8_t  adcN = 0;
static uint16_t lastAdc = 0;
static uint32_t lastSampleMs = 0;
static bool     haveVolt = false;
static float    voltFast = 0, voltSlow = 0;

static uint32_t cntRequests = 0, cntAnswers = 0;
static uint32_t lastPrintMs = 0;

// ---------------------------------------------------------------------------
//  Voltage
// ---------------------------------------------------------------------------
static void sampleVoltage() {
  const uint32_t now = millis();
  if (now - lastSampleMs < SAMPLE_EVERY_MS) return;
  lastSampleMs = now;

  adcSum += analogRead(PIN_VSENSE);
  if (++adcN < SAMPLES_PER_AVG) return;

  const float adc = adcSum / (float)SAMPLES_PER_AVG;
  adcSum = 0;
  adcN = 0;
  lastAdc = (uint16_t)(adc + 0.5f);

  const float v = adc * (VREF_V / 1023.0f) * DIVIDER * VOLTAGE_CAL;
  if (v < MIN_VALID_V) { haveVolt = false; return; }   // divider not connected / bad reading

  const float dt = SAMPLE_EVERY_MS * SAMPLES_PER_AVG;
  if (!haveVolt) {
    voltFast = voltSlow = v;
    haveVolt = true;
  } else {
    voltFast += (v - voltFast) * (dt / (VOLT_FILTER_MS + dt));
    voltSlow += (v - voltSlow) * (dt / (SOC_FILTER_MS + dt));
  }
}

static uint8_t socFromVoltage(float packV) {
  const uint8_t n = sizeof(SOC_MV) / sizeof(SOC_MV[0]);
  const float cellMv = packV * 1000.0f / CELLS_S;
  // map CELL_EMPTY_MV..CELL_FULL_MV onto the table's range
  const float mv = SOC_MV[0] + (cellMv - CELL_EMPTY_MV) *
                   ((float)(SOC_MV[n - 1] - SOC_MV[0]) / (CELL_FULL_MV - CELL_EMPTY_MV));
  if (mv <= SOC_MV[0])     return SOC_PCT[0];
  if (mv >= SOC_MV[n - 1]) return SOC_PCT[n - 1];
  for (uint8_t i = 1; i < n; i++) {
    if (mv < SOC_MV[i]) {
      const float f = (mv - SOC_MV[i - 1]) / (float)(SOC_MV[i] - SOC_MV[i - 1]);
      return (uint8_t)(SOC_PCT[i - 1] + f * (SOC_PCT[i] - SOC_PCT[i - 1]) + 0.5f);
    }
  }
  return SOC_PCT[n - 1];
}

// Values to send: measured, or the fallback while the reading is invalid
static void currentValues(uint8_t &volts, uint8_t &soc) {
  if (haveVolt) {
    volts = (uint8_t)constrain((long)(voltFast + 0.5f), 0L, 255L);
    soc   = socFromVoltage(voltSlow);
  } else {
    volts = FALLBACK_V;
    soc   = FALLBACK_SOC;
  }
}

// ---------------------------------------------------------------------------
//  Soco bus
// ---------------------------------------------------------------------------
static void startIfHeader(uint8_t b) {
  if (b == 0xC5 || b == 0xB6) { t0 = b; st = ST_T1; }
  else st = ST_T0;
}

// Returns true when a valid BATTERY POLL is complete
static bool feed(uint8_t b) {
  switch (st) {
    case ST_T0:   startIfHeader(b); return false;
    case ST_T1:
      if ((t0 == 0xC5 && b == 0x5C) || (t0 == 0xB6 && b == 0x6B)) st = ST_DEST;
      else startIfHeader(b);
      return false;
    case ST_DEST: dest = b; st = ST_SRC; return false;
    case ST_SRC:  src = b;  st = ST_LEN; return false;
    case ST_LEN:
      if (b > 32) { startIfHeader(b); return false; }
      len = b; calc = b; idx = 0;
      st = (len == 0) ? ST_CHK : ST_DATA;
      return false;
    case ST_DATA:
      calc ^= b;
      if (++idx >= len) st = ST_CHK;
      return false;
    case ST_CHK:  chk = b; st = ST_END; return false;
    case ST_END:
      st = ST_T0;
      if (b == 0x0D && chk == calc) return (t0 == 0xC5 && dest == 0x5A);
      startIfHeader(b);
      return false;
  }
  st = ST_T0;
  return false;
}

static void buildAnswer(uint8_t to) {
  uint8_t volts, soc;
  currentValues(volts, soc);
  frame[0] = 0xB6; frame[1] = 0x6B; frame[2] = to; frame[3] = 0x5A; frame[4] = 10;
  frame[5]  = volts;
  frame[6]  = soc;
  frame[7]  = (uint8_t)(int8_t)TEMP_C;
  frame[8]  = 0;                         // current (A)
  frame[9]  = 0;                         // charge cycles
  frame[10] = 0;
  frame[11] = 0;                         // discharge cycles
  frame[12] = 0;
  frame[13] = 0;                         // no errors
  frame[14] = 0;                         // not charging
  uint8_t c = frame[4];
  for (uint8_t i = 5; i < 15; i++) c ^= frame[i];
  frame[15] = c;
  frame[16] = 0x0D;
}

static void sendAnswer() {
  digitalWrite(PIN_DIR, HIGH);           // MAX485 transmitter on
  delayMicroseconds(20);
  bus.write(frame, sizeof(frame));
  bus.flushOutput();                     // waits until the last stop bit is out
  delayMicroseconds(50);
  digitalWrite(PIN_DIR, LOW);            // back to listening
  lastAnswerMs = millis();
  cntAnswers++;
}

// ---------------------------------------------------------------------------
//  Status
// ---------------------------------------------------------------------------
static void printStatus() {
  uint8_t volts, soc;
  currentValues(volts, soc);
  if (haveVolt) {
    Serial.print(F("Pack "));   Serial.print(voltFast, 1);
    Serial.print(F("V  cell ")); Serial.print(voltSlow / CELLS_S, 3);
    Serial.print(F("V  ADC "));  Serial.print(lastAdc);
  } else {
    Serial.print(F("No valid voltage (ADC ")); Serial.print(lastAdc); Serial.print(')');
  }
  Serial.print(F(" -> sending ")); Serial.print(volts);
  Serial.print(F("V ")); Serial.print(soc); Serial.print('%');
  Serial.print(F(" | ECU polls ")); Serial.print(cntRequests);
  Serial.print(F(", answers "));    Serial.println(cntAnswers);
}

// LED: slow blink              = no ECU polls
//      fast blink              = ECU ok, no valid voltage (sending 83 V / 100 %)
//      steady on + short blink = ECU ok, voltage ok
static void serviceLed(uint32_t nowMs) {
  bool on;
  if (nowMs - lastAnswerMs > 3000) on = (nowMs / 500) & 1;
  else if (!haveVolt)              on = (nowMs / 100) & 1;
  else                             on = (nowMs - lastAnswerMs) > 40;
  digitalWrite(PIN_LED, on ? HIGH : LOW);
}

// ---------------------------------------------------------------------------
void setup() {
  pinMode(PIN_DIR, OUTPUT);
  digitalWrite(PIN_DIR, LOW);            // listen
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_VSENSE, INPUT);
  Serial.begin(115200);
  bus.begin(9600);
  pinMode(8, INPUT_PULLUP);
  Serial.println(F("SocoTC_Voltage started: pack voltage on A0 (270k/10k divider)"));
}

void loop() {
  const uint32_t nowMs = millis();

  if (st != ST_T0 && nowMs - lastByteMs > 20) st = ST_T0;   // drop a half-received telegram

  while (bus.available()) {
    const uint8_t b = (uint8_t)bus.read();
    lastByteMs = millis();
    if (feed(b)) {
      cntRequests++;
      buildAnswer(src);
      answerPending = true;
      answerAtUs = micros() + RESPONSE_DELAY_US;
    }
  }

  if (answerPending && (int32_t)(micros() - answerAtUs) >= 0) {
    answerPending = false;
    sendAnswer();
  }

  sampleVoltage();

  if (millis() - lastPrintMs >= 1000 && !answerPending) {
    lastPrintMs = millis();
    printStatus();
  }

  serviceLed(millis());
}
