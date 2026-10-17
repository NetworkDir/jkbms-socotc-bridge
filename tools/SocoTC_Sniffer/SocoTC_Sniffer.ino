/*
  ============================================================================
   SocoTC_Sniffer - Super Soco RS485 bus listener / timing meter
   Board: Arduino Nano + MAX485
  ============================================================================

   Prints every telegram on the bus to the USB serial port together with a
   timestamp and the GAP since the previous telegram. Every 5 seconds it prints
   a summary:

     cycle          : time between two battery polls (one full ECU cycle)
     batt.ans->gap  : how long the bus stays idle after the battery answer
                      >>> this is the window available for reading the JK <<<
     batt.no-answer : how long the ECU waits when the battery does not answer
     longest gap    : longest silence between any two telegrams

   Wiring (same as the emulator):
     MAX485 RO -> D8, DI -> D9, DE+RE -> D2, A/B -> Soco RS485 A/B, common GND

   Serial Monitor: 115200 baud

   With ANSWER_BATTERY 1 it answers the battery poll with fixed values
   (72 V, 80 %), so the bike runs normally and "batt.ans->gap" can be measured.
   Set it to 0 if another battery or emulator on the bus IS ANSWERING.

   Required library: AltSoftSerial (Paul Stoffregen)
*/

#include <AltSoftSerial.h>

// ============================================================================
//  SETTINGS
// ============================================================================
#define PIN_RS485_DIR      2
#define PIN_LED            13
#define USB_BAUD           115200
#define VERBOSE            1        // 1 = print every telegram, 0 = summary only
#define SUMMARY_MS         5000UL   // summary interval

#define ANSWER_BATTERY     1        // 1 = answer the battery poll
#define ANS_VOLTAGE        72
#define ANS_SOC            80
#define ANS_TEMP           25
#define RESPONSE_DELAY_US  1000UL

// ============================================================================
AltSoftSerial bus;   // RX = D8, TX = D9

static const uint32_t BYTE_US       = 1042;   // 1 byte at 9600 baud
static const uint32_t TG_TIMEOUT_US = 5000;   // this much silence mid-telegram -> incomplete

enum Kind : uint8_t {
  K_NONE, K_SPEEDO_REQ, K_SPEEDO_RES, K_CTRL_REQ, K_CTRL_RES,
  K_BATT_REQ, K_BATT_RES, K_OTHER, K_OURS
};

struct Stat {
  uint32_t mn, mx, sum, n;
  void reset() { mn = 0xFFFFFFFFUL; mx = 0; sum = 0; n = 0; }
  void add(uint32_t v) { if (v < mn) mn = v; if (v > mx) mx = v; sum += v; n++; }
};

// receiver
static uint8_t  tg[40];
static uint8_t  tgN = 0, tgExpected = 0;
static uint32_t tgStartUs = 0, tgStartMs = 0;
static uint8_t  junk[48];
static uint8_t  junkN = 0;
static uint32_t junkStartMs = 0;
static uint32_t lastByteUs = 0;

// timing
static uint32_t lastBusEndUs = 0;
static bool     haveBusEnd   = false;
static Kind     prevKind     = K_NONE;
static uint32_t lastBattReqUs = 0;
static bool     haveBattReq  = false;

static Stat stCycle, stAfterBattRes, stBattNoAnswer, stGap;
static uint32_t cntOk = 0, cntBadChk = 0, cntIncomplete = 0, cntJunk = 0;
static uint32_t lastSummaryMs = 0;

// answer
static uint8_t  txBuf[17];
static bool     respPending = false;
static uint32_t respAtUs    = 0;
static uint8_t  respDest    = 0xAA;
static bool     txActive    = false;
static uint32_t txEndUs     = 0;

// ============================================================================
//  Print helpers
// ============================================================================
static void printMs1(uint32_t us) {          // print microseconds as "12.3" ms
  Serial.print(us / 1000);
  Serial.print('.');
  Serial.print((us / 100) % 10);
}

static void printHex(const uint8_t *b, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    if (b[i] < 0x10) Serial.print('0');
    Serial.print(b[i], HEX);
    Serial.print(' ');
  }
}

static void printStat(const Stat &s) {
  if (s.n == 0) { Serial.print('-'); return; }
  printMs1(s.mn);           Serial.print('/');
  printMs1(s.sum / s.n);    Serial.print('/');
  printMs1(s.mx);
}

static Kind kindOf(const uint8_t *b, uint8_t n) {
  if (n < 4) return K_OTHER;
  if (b[0] == 0xC5 && b[1] == 0x5C) {
    if (b[2] == 0xBA) return K_SPEEDO_REQ;
    if (b[2] == 0xDA) return K_CTRL_REQ;
    if (b[2] == 0x5A) return K_BATT_REQ;
  } else if (b[0] == 0xB6 && b[1] == 0x6B) {
    if (b[3] == 0xBA) return K_SPEEDO_RES;
    if (b[3] == 0xDA) return K_CTRL_RES;
    if (b[3] == 0x5A) return K_BATT_RES;
  }
  return K_OTHER;
}

static const __FlashStringHelper *kindName(Kind k) {
  switch (k) {
    case K_SPEEDO_REQ: return F("ECU->Dashboard  poll");
    case K_SPEEDO_RES: return F("Dashboard->ECU  resp");
    case K_CTRL_REQ:   return F("ECU->Controller poll");
    case K_CTRL_RES:   return F("Controller->ECU resp");
    case K_BATT_REQ:   return F("ECU->Battery    poll");
    case K_BATT_RES:   return F("Battery->ECU    resp");
    case K_OURS:       return F("OURS->ECU  batt resp");
    default:           return F("Unknown             ");
  }
}

// ============================================================================
//  Event handling
// ============================================================================
// status: 0 = ok, 1 = checksum error, 2 = incomplete
static void onTelegram(Kind k, uint32_t startUs, uint32_t endUs, uint32_t startMs,
                       const uint8_t *b, uint8_t n, uint8_t status) {
  uint32_t gap = 0;
  const bool haveGap = haveBusEnd;
  if (haveGap) {
    gap = ((int32_t)(startUs - lastBusEndUs) > 0) ? (startUs - lastBusEndUs) : 0;
    stGap.add(gap);
    if (prevKind == K_BATT_RES || prevKind == K_OURS) stAfterBattRes.add(gap);
    if (prevKind == K_BATT_REQ && k != K_BATT_RES && k != K_OURS) stBattNoAnswer.add(gap);
  }
  if (k == K_BATT_REQ) {
    if (haveBattReq) stCycle.add(startUs - lastBattReqUs);
    lastBattReqUs = startUs;
    haveBattReq = true;
  }

  if (status == 0) cntOk++;
  else if (status == 1) cntBadChk++;
  else cntIncomplete++;

#if VERBOSE
  Serial.print(startMs);
  Serial.print(F(" ms  +"));
  if (haveGap) printMs1(gap); else Serial.print('-');
  Serial.print(F(" ms\t"));
  Serial.print(kindName(k));
  Serial.print(F("  "));
  printHex(b, n);
  if (status == 1) Serial.print(F(" <CHECKSUM ERROR>"));
  if (status == 2) Serial.print(F(" <INCOMPLETE>"));
  if ((k == K_BATT_RES || k == K_OURS) && status == 0 && n >= 15) {
    Serial.print(F(" -> "));
    Serial.print(b[5]);  Serial.print(F("V "));
    Serial.print(b[6]);  Serial.print(F("% "));
    Serial.print((int8_t)b[7]); Serial.print('C');
  }
  Serial.println();
#endif

  lastBusEndUs = endUs;
  haveBusEnd = true;
  prevKind = k;
  if (status == 0) digitalWrite(PIN_LED, !digitalRead(PIN_LED));
}

static void flushJunk() {
  if (!junkN) return;
  cntJunk += junkN;
#if VERBOSE
  Serial.print(junkStartMs);
  Serial.print(F(" ms  ?? junk ("));
  Serial.print(junkN);
  Serial.print(F(" bytes): "));
  printHex(junk, junkN);
  Serial.println();
#endif
  junkN = 0;
}

static void addJunk(uint8_t b) {
  if (junkN == 0) junkStartMs = millis();
  if (junkN < sizeof(junk)) junk[junkN++] = b;
  else { flushJunk(); junkStartMs = millis(); junk[junkN++] = b; }
}

static uint8_t xorChecksum(const uint8_t *b, uint8_t n) {   // b[4] = len, b[5..] = data
  uint8_t c = b[4];
  for (uint8_t i = 0; i < b[4]; i++) c ^= b[5 + i];
  (void)n;
  return c;
}

static void scheduleBatteryAnswer(uint8_t dest) {
#if ANSWER_BATTERY
  const uint8_t p[10] = { ANS_VOLTAGE, ANS_SOC, (uint8_t)(int8_t)ANS_TEMP, 0, 0, 0, 0, 0, 0, 0 };
  txBuf[0] = 0xB6; txBuf[1] = 0x6B; txBuf[2] = dest; txBuf[3] = 0x5A; txBuf[4] = 10;
  uint8_t c = 10;
  for (uint8_t i = 0; i < 10; i++) { txBuf[5 + i] = p[i]; c ^= p[i]; }
  txBuf[15] = c;
  txBuf[16] = 0x0D;
  respDest = dest;
  respPending = true;
  respAtUs = micros() + RESPONSE_DELAY_US;
#else
  (void)dest;
#endif
}

static void finishTelegram(uint32_t endUs, bool complete) {
  uint8_t status = 2;
  if (complete) {
    const bool ok = tg[tgN - 1] == 0x0D && tg[tgN - 2] == xorChecksum(tg, tgN);
    status = ok ? 0 : 1;
  }
  const Kind k = kindOf(tg, tgN);
  onTelegram(k, tgStartUs, endUs, tgStartMs, tg, tgN, status);
  if (k == K_BATT_REQ && status == 0) scheduleBatteryAnswer(tg[3]);
  tgN = 0;
  tgExpected = 0;
}

static void onByte(uint8_t b, uint32_t t) {
  lastByteUs = t;

  if (tgN == 0) {
    if (b == 0xC5 || b == 0xB6) {
      flushJunk();
      tg[tgN++] = b;
      tgStartUs = t - BYTE_US;           // start of the byte (it is complete when read)
      tgStartMs = millis();
    } else {
      addJunk(b);
    }
    return;
  }

  if (tgN == 1) {
    if ((tg[0] == 0xC5 && b == 0x5C) || (tg[0] == 0xB6 && b == 0x6B)) {
      tg[tgN++] = b;
    } else {
      addJunk(tg[0]);
      tgN = 0;
      onByte(b, t);                      // this byte may start a new telegram
    }
    return;
  }

  tg[tgN++] = b;
  if (tgN == 5) {
    if (b > 32) {                        // invalid length -> junk
      for (uint8_t i = 0; i < tgN; i++) addJunk(tg[i]);
      tgN = 0;
      return;
    }
    tgExpected = 5 + b + 2;
  }
  if (tgN >= 5 && tgN >= tgExpected) finishTelegram(t, true);
}

// ============================================================================
static void serviceTx() {
  if (respPending && (int32_t)(micros() - respAtUs) >= 0) {
    respPending = false;
    digitalWrite(PIN_RS485_DIR, HIGH);
    delayMicroseconds(20);
    const uint32_t startUs = micros();
    bus.write(txBuf, sizeof(txBuf));
    txEndUs  = startUs + sizeof(txBuf) * BYTE_US;
    txActive = true;
    onTelegram(K_OURS, startUs, txEndUs, millis(), txBuf, sizeof(txBuf), 0);
  }
  if (txActive && (int32_t)(micros() - (txEndUs + 1200)) >= 0) {
    digitalWrite(PIN_RS485_DIR, LOW);
    txActive = false;
  }
}

static void printSummary() {
  Serial.print(F("=== SUMMARY  cycle="));         printStat(stCycle);
  Serial.print(F(" ms | batt.ans->gap="));        printStat(stAfterBattRes);
  Serial.print(F(" ms | batt.no-answer wait="));  printStat(stBattNoAnswer);
  Serial.print(F(" ms | longest gap="));
  if (stGap.n) printMs1(stGap.mx); else Serial.print('-');
  Serial.print(F(" ms | tg ok="));    Serial.print(cntOk);
  Serial.print(F(" chk="));           Serial.print(cntBadChk);
  Serial.print(F(" incomplete="));    Serial.print(cntIncomplete);
  Serial.print(F(" junk="));          Serial.print(cntJunk);
  Serial.println(F(" bytes  (min/avg/max)"));

  stCycle.reset(); stAfterBattRes.reset(); stBattNoAnswer.reset(); stGap.reset();
  cntOk = cntBadChk = cntIncomplete = cntJunk = 0;
}

void setup() {
  pinMode(PIN_RS485_DIR, OUTPUT);
  digitalWrite(PIN_RS485_DIR, LOW);      // listen
  pinMode(PIN_LED, OUTPUT);
  Serial.begin(USB_BAUD);
  bus.begin(9600);
  pinMode(8, INPUT_PULLUP);
  stCycle.reset(); stAfterBattRes.reset(); stBattNoAnswer.reset(); stGap.reset();

  Serial.println(F("SocoTC_Sniffer started. Columns: time  +gap  type  bytes"));
#if ANSWER_BATTERY
  Serial.println(F("Answering battery polls with 72V / 80% (ANSWER_BATTERY 1)."));
#endif
}

void loop() {
  while (bus.available()) onByte((uint8_t)bus.read(), micros());

  const uint32_t now = micros();
  if (tgN && (now - lastByteUs) > TG_TIMEOUT_US) finishTelegram(lastByteUs, false);
  if (junkN && (now - lastByteUs) > TG_TIMEOUT_US) flushJunk();

  serviceTx();

  if (millis() - lastSummaryMs >= SUMMARY_MS) {
    lastSummaryMs = millis();
    printSummary();
  }
}
