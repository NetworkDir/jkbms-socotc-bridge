/*
  ============================================================================
   JK_Soco_Final - JK BMS (UART) -> Super Soco TC (RS485)
   Board: Arduino Nano + MAX485.  D0/D1 are NOT used (USB stays free).
  ============================================================================

   - Answers the ECU's battery poll (C5 5C 5A AA ...) with values read from the JK.
   - Right after the first answer, then every JK_EVERY_N_CYCLES answers, while the
     ECU is silent for ~91 ms, it sends a "read all data" request to the JK over
     UART and reads the reply. Interrupts are disabled during the JK read; D4/D5
     are bit-banged with clock accuracy using the Timer1 counter (SoftwareSerial
     is not reliable enough at 115200).
   - If no JK data has been received yet, or none for 10 s, it sends 83 V / 100 %.

   Wiring:
     MAX485:
       RO      -> D8
       DI      <- D9
       DE + RE <- D2   (bridged)
       VCC/GND -> 5V / GND
       A / B   -> Soco RS485 A / B
     JK 4-pin socket (GND - RX - TX - VBAT):
       JK TX   -> D4
       JK RX   <- D5
       JK GND  -> Nano GND
       JK VBAT -> NOT CONNECTED

   Library: AltSoftSerial (Paul Stoffregen)
   Serial Monitor (115200): one status line per JK read.
*/

#include <AltSoftSerial.h>
#include "config.h"
#include "jk_bms.h"
#include "soco_tc.h"
#include "convert.h"

AltSoftSerial bus;            // MAX485: RX = D8, TX = D9 (fixed)

// JK UART pins (PORTD bits): D4 = PD4 (from JK TX), D5 = PD5 (to JK RX)
#define JK_RX_BIT  4
#define JK_TX_BIT  5

static jk::Receiver         jkRx;
static jk::Data             jkData;
static soco::Parser         socoRx;
static conv::ChargeDetector chargeDet;
static uint8_t              jkRaw[jk::RX_BUF_SIZE];

static bool     haveJk         = false;
static uint32_t lastJkOkMs     = 0;
static uint8_t  cycleCount     = JK_EVERY_N_CYCLES - 1;   // read the JK right after the first answer
static uint32_t lastSocoByteMs = 0;

static bool     respPending    = false;
static uint32_t respAtUs       = 0;
static uint8_t  respDest       = soco::ADDR_ECU;
static uint32_t lastAnswerMs   = 0;

static uint32_t answerCount = 0, jkOkCount = 0, jkFailCount = 0;

// ---------------------------------------------------------------------------
//  JK UART (115200) on the Timer1 counter. Must be called with interrupts DISABLED.
//  AltSoftSerial runs Timer1 without a prescaler (16 MHz) at 9600; we only read it.
// ---------------------------------------------------------------------------
static const uint16_t BIT_CY = (F_CPU + JK_BAUD / 2) / JK_BAUD;   // 139 clocks = 1 bit

static void jkSendByte(uint8_t b) {
  uint16_t t = TCNT1;
  PORTD &= ~_BV(JK_TX_BIT);                         // start bit
  t += BIT_CY; while ((int16_t)(TCNT1 - t) < 0) {}
  for (uint8_t i = 0; i < 8; i++) {
    if (b & 1) PORTD |= _BV(JK_TX_BIT); else PORTD &= ~_BV(JK_TX_BIT);
    b >>= 1;
    t += BIT_CY; while ((int16_t)(TCNT1 - t) < 0) {}
  }
  PORTD |= _BV(JK_TX_BIT);                          // stop bit
  t += BIT_CY; while ((int16_t)(TCNT1 - t) < 0) {}
}

// Collect the raw JK reply. Returns once the frame length is reached or on timeout.
static uint16_t jkReceive(uint32_t timeoutCy, uint32_t &firstCy, uint32_t &doneCy) {
  uint16_t n = 0, expected = 0;
  uint16_t last = TCNT1;
  uint32_t elapsed = 0;
  firstCy = doneCy = 0;

  while (elapsed < timeoutCy) {
    const uint16_t now = TCNT1;
    elapsed += (uint16_t)(now - last);
    last = now;
    if (PIND & _BV(JK_RX_BIT)) continue;            // line idle (high)

    // start bit: sample every bit in its middle
    uint16_t t = now + BIT_CY + BIT_CY / 2;
    uint8_t b = 0;
    for (uint8_t i = 0; i < 8; i++) {
      while ((int16_t)(TCNT1 - t) < 0) {}
      b >>= 1;
      if (PIND & _BV(JK_RX_BIT)) b |= 0x80;
      t += BIT_CY;
    }
    while ((int16_t)(TCNT1 - t) < 0) {}             // middle of the stop bit

    if (n == 0 && b != 0x4E) { /* not a header, drop it */ }
    else if (n == 1 && b != 0x57) { n = 0; }
    else {
      if (n == 0) firstCy = elapsed;
      jkRaw[n++] = b;
      if (n == 4) {
        expected = (((uint16_t)jkRaw[2] << 8) | jkRaw[3]) + 2;
        if (expected < 21 || expected > sizeof(jkRaw)) { n = 0; expected = 0; }
      }
      if (expected && n >= expected) {
        const uint16_t e = TCNT1; elapsed += (uint16_t)(e - last);
        doneCy = elapsed;
        break;
      }
    }
    const uint16_t now2 = TCNT1;
    elapsed += (uint16_t)(now2 - last);
    last = now2;
  }
  return n;
}

// ---------------------------------------------------------------------------
static bool jkFresh() {
  return haveJk && (millis() - lastJkOkMs) < JK_STALE_MS;
}

static soco::BatteryStatus currentStatus() {
  jk::Data d;
  const bool useJk = jkFresh();
  if (useJk) d = jkData;
  else conv::fillDefaults(d);
  return conv::toSoco(d, useJk && chargeDet.charging());
}

// ---------------------------------------------------------------------------
//  JK read. Called right after a battery answer (the ECU is silent for ~91 ms).
// ---------------------------------------------------------------------------
static void readJk() {
  uint8_t req[jk::READ_ALL_LEN];
  const uint8_t reqLen = jk::buildReadAllRequest(req, JK_FRAME_SOURCE);
  const uint32_t timeoutCy = (uint32_t)JK_RX_WINDOW_MS * (F_CPU / 1000UL);
  uint32_t firstCy, doneCy;

  const uint8_t sreg = SREG;
  cli();                                            // no interrupts during the window
  for (uint8_t i = 0; i < reqLen; i++) jkSendByte(req[i]);
  const uint16_t rxBytes = jkReceive(timeoutCy, firstCy, doneCy);
  SREG = sreg;                                      // interrupts back on

  bool ok = false, badFrame = false;
  jkRx.reset();
  for (uint16_t i = 0; i < rxBytes && i < sizeof(jkRaw); i++) {
    const jk::Receiver::Result r = jkRx.feed(jkRaw[i]);
    if (r == jk::Receiver::FRAME_OK) {
      jk::Data d;
      if (jk::decode(jkRx.frame(), jkRx.frameLen(), d, JK_DEFAULT_PROTOCOL)) { jkData = d; ok = true; }
      else badFrame = true;
      break;
    }
    if (r == jk::Receiver::FRAME_BAD) { badFrame = true; break; }
  }

  if (ok) {
    haveJk = true;
    lastJkOkMs = millis();
    jkOkCount++;
    chargeDet.update(jkData.current_cA, lastJkOkMs);
  } else {
    jkFailCount++;
  }

#if DEBUG_SERIAL
  const uint32_t cyPerMs = F_CPU / 1000UL;
  if (ok) {
    Serial.print(F("JK OK  "));
    Serial.print(jkData.voltage_cV / 100); Serial.print('.');
    const uint8_t r = jkData.voltage_cV % 100; if (r < 10) Serial.print('0'); Serial.print(r);
    Serial.print(F("V  ")); Serial.print(jkData.current_cA / 100);
    Serial.print(F("A  ")); Serial.print(jkData.soc); Serial.print('%');
    Serial.print(F("  ")); Serial.print(conv::pickTemperature(jkData)); Serial.print('C');
    Serial.print(F("  ")); Serial.print(jkData.cellCount); Serial.print(F(" cells"));
    Serial.print(F(" | took ")); Serial.print(doneCy / cyPerMs); Serial.print(F(" ms"));
  } else {
    Serial.print(F("NO JK (sending 83V 100%)  got ")); Serial.print(rxBytes); Serial.print(F(" bytes"));
    if (rxBytes) { Serial.print(F(", first byte after ")); Serial.print(firstCy / cyPerMs); Serial.print(F(" ms")); }
    if (badFrame) Serial.print(F(", bad frame"));
  }
  Serial.print(F(" | ECU answers ")); Serial.print(answerCount);
  Serial.print(F(", jk ok/fail ")); Serial.print(jkOkCount);
  Serial.print('/'); Serial.println(jkFailCount);
#endif
}

// ---------------------------------------------------------------------------
static void answerBattery() {
  uint8_t buf[soco::BATTERY_FRAME_LEN];
  const uint8_t n = soco::buildBatteryResponse(currentStatus(), respDest, buf);

  digitalWrite(PIN_RS485_DIR, HIGH);      // MAX485 transmitter on
  delayMicroseconds(20);
  bus.write(buf, n);
  bus.flushOutput();                      // waits until the last stop bit is out
  delayMicroseconds(50);
  digitalWrite(PIN_RS485_DIR, LOW);       // back to listening

  lastAnswerMs = millis();
  answerCount++;

  if (++cycleCount >= JK_EVERY_N_CYCLES) {
    cycleCount = 0;
    readJk();
    socoRx.reset();
    lastSocoByteMs = millis();
  }
}

// ---------------------------------------------------------------------------
//  LED: slow blink              = no ECU polls
//       fast blink              = ECU ok, no JK data (sending 100 %)
//       steady on + short blink = ECU and JK ok
// ---------------------------------------------------------------------------
static void serviceLed(uint32_t nowMs) {
  bool on;
  if (nowMs - lastAnswerMs > 3000) on = (nowMs / 500) & 1;
  else if (!jkFresh())             on = (nowMs / 100) & 1;
  else                             on = (nowMs - lastAnswerMs) > 40;
  digitalWrite(PIN_LED, on ? HIGH : LOW);
}

// ---------------------------------------------------------------------------
void setup() {
  pinMode(PIN_RS485_DIR, OUTPUT);
  digitalWrite(PIN_RS485_DIR, LOW);       // listen
  pinMode(PIN_LED, OUTPUT);

  pinMode(4, INPUT_PULLUP);               // JK TX -> D4
  pinMode(5, OUTPUT);                     // D5 -> JK RX
  digitalWrite(5, HIGH);                  // UART idle level

  Serial.begin(115200);
  bus.begin(SOCO_BAUD);                   // puts Timer1 into 16 MHz free-running mode
  pinMode(8, INPUT_PULLUP);

#if DEBUG_SERIAL
  Serial.println(F("JK_Soco_Final started. MAX485 D8/D9, JK D4/D5. Without JK: 83V 100%."));
#endif
}

void loop() {
  const uint32_t nowMs = millis();

  if (socoRx.busy() && nowMs - lastSocoByteMs > 20) socoRx.reset();

  while (bus.available()) {
    const uint8_t b = (uint8_t)bus.read();
    lastSocoByteMs = millis();
    if (socoRx.feed(b)) {
      const soco::Telegram &t = socoRx.telegram();
      if (soco::isRequestFor(t, soco::ADDR_BATTERY)) {
        respDest = t.src;
        respPending = true;
        respAtUs = micros() + RESPONSE_DELAY_US;
      }
    }
  }

  if (respPending && (int32_t)(micros() - respAtUs) >= 0) {
    respPending = false;
    answerBattery();
  }

  serviceLed(nowMs);
}
