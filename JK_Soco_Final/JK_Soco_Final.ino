/*
  ============================================================================
   JK_Soco_Final - JK BMS (UART) -> Super Soco TC (RS485)
   Kart: Arduino Nano + MAX485.  D0/D1 KULLANILMAZ (USB serbest).
  ============================================================================

   - ECU'nun batarya sorgusuna (C5 5C 5A AA ...) JK'dan okunan degerlerle cevap verir.
   - Ilk cevaptan hemen sonra, sonra her JK_EVERY_N_CYCLES cevapta bir, ECU ~91 ms
     susarken JK'ya UART'tan "tum veriyi oku" istegi atar ve cevabi okur.
     JK okunurken kesmeler kapatilir; D4/D5 Timer1 sayaciyla bit bit, saat
     hassasiyetinde surulur (115200 icin SoftwareSerial yeterince guvenilir degil).
   - JK verisi hic gelmediyse ya da 10 sn gelmezse 83 V / %100 gonderir.

   Baglanti:
     MAX485 (degismedi):
       RO      -> D8
       DI      <- D9
       DE + RE <- D2   (kopru)
       VCC/GND -> 5V / GND
       A / B   -> Soco RS485 A / B
     JK 4 pinli soket (GND - RX - TX - VBAT):
       JK TX   -> D4
       JK RX   <- D5
       JK GND  -> Nano GND
       JK VBAT -> BOS

   Kutuphane: AltSoftSerial (Paul Stoffregen)
   Seri Monitor (115200): her JK okumasinda bir durum satiri.
*/

#include <AltSoftSerial.h>
#include "config.h"
#include "jk_bms.h"
#include "soco_tc.h"
#include "convert.h"

AltSoftSerial bus;            // MAX485: RX = D8, TX = D9 (sabit)

// JK UART pinleri (PORTD bitleri): D4 = PD4 (JK TX'ten gelir), D5 = PD5 (JK RX'e gider)
#define JK_RX_BIT  4
#define JK_TX_BIT  5

static jk::Receiver         jkRx;
static jk::Data             jkData;
static soco::Parser         socoRx;
static conv::ChargeDetector chargeDet;
static uint8_t              jkRaw[jk::RX_BUF_SIZE];

static bool     haveJk         = false;
static uint32_t lastJkOkMs     = 0;
static uint8_t  cycleCount     = JK_EVERY_N_CYCLES - 1;   // ilk cevaptan sonra hemen JK oku
static uint32_t lastSocoByteMs = 0;

static bool     respPending    = false;
static uint32_t respAtUs       = 0;
static uint8_t  respDest       = soco::ADDR_ECU;
static uint32_t lastAnswerMs   = 0;

static uint32_t answerCount = 0, jkOkCount = 0, jkFailCount = 0;

// ---------------------------------------------------------------------------
//  JK UART (115200) - Timer1 sayaci ile, kesmeler KAPALIYKEN cagrilmali.
//  AltSoftSerial Timer1'i 9600 icin bolucusuz (16 MHz) calistirir; biz sadece okuyoruz.
// ---------------------------------------------------------------------------
static const uint16_t BIT_CY = (F_CPU + JK_BAUD / 2) / JK_BAUD;   // 139 saat = 1 bit

static void jkSendByte(uint8_t b) {
  uint16_t t = TCNT1;
  PORTD &= ~_BV(JK_TX_BIT);                         // start biti
  t += BIT_CY; while ((int16_t)(TCNT1 - t) < 0) {}
  for (uint8_t i = 0; i < 8; i++) {
    if (b & 1) PORTD |= _BV(JK_TX_BIT); else PORTD &= ~_BV(JK_TX_BIT);
    b >>= 1;
    t += BIT_CY; while ((int16_t)(TCNT1 - t) < 0) {}
  }
  PORTD |= _BV(JK_TX_BIT);                          // stop biti
  t += BIT_CY; while ((int16_t)(TCNT1 - t) < 0) {}
}

// JK cevabini ham olarak topla. Cerceve uzunlugu dolunca ya da sure bitince doner.
static uint16_t jkReceive(uint32_t timeoutCy, uint32_t &firstCy, uint32_t &doneCy) {
  uint16_t n = 0, expected = 0;
  uint16_t last = TCNT1;
  uint32_t elapsed = 0;
  firstCy = doneCy = 0;

  while (elapsed < timeoutCy) {
    const uint16_t now = TCNT1;
    elapsed += (uint16_t)(now - last);
    last = now;
    if (PIND & _BV(JK_RX_BIT)) continue;            // hat bos (yuksek)

    // start biti: her bitin ortasindan ornek al
    uint16_t t = now + BIT_CY + BIT_CY / 2;
    uint8_t b = 0;
    for (uint8_t i = 0; i < 8; i++) {
      while ((int16_t)(TCNT1 - t) < 0) {}
      b >>= 1;
      if (PIND & _BV(JK_RX_BIT)) b |= 0x80;
      t += BIT_CY;
    }
    while ((int16_t)(TCNT1 - t) < 0) {}             // stop bitinin ortasi

    if (n == 0 && b != 0x4E) { /* baslik degil, at */ }
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
//  JK okuma. Batarya cevabindan hemen sonra cagrilir (ECU ~91 ms susuyor).
// ---------------------------------------------------------------------------
static void readJk() {
  uint8_t req[jk::READ_ALL_LEN];
  const uint8_t reqLen = jk::buildReadAllRequest(req, JK_FRAME_SOURCE);
  const uint32_t timeoutCy = (uint32_t)JK_RX_WINDOW_MS * (F_CPU / 1000UL);
  uint32_t firstCy, doneCy;

  const uint8_t sreg = SREG;
  cli();                                            // pencere boyunca kesme yok
  for (uint8_t i = 0; i < reqLen; i++) jkSendByte(req[i]);
  const uint16_t rxBytes = jkReceive(timeoutCy, firstCy, doneCy);
  SREG = sreg;                                      // kesmeler geri

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
    Serial.print(F("A  %")); Serial.print(jkData.soc);
    Serial.print(F("  ")); Serial.print(conv::pickTemperature(jkData)); Serial.print('C');
    Serial.print(F("  ")); Serial.print(jkData.cellCount); Serial.print(F(" hucre"));
    Serial.print(F(" | sure ")); Serial.print(doneCy / cyPerMs); Serial.print(F(" ms"));
  } else {
    Serial.print(F("JK YOK (83V %100 gonderiliyor)  gelen ")); Serial.print(rxBytes); Serial.print(F(" bayt"));
    if (rxBytes) { Serial.print(F(", ilk bayt ")); Serial.print(firstCy / cyPerMs); Serial.print(F(" ms")); }
    if (badFrame) Serial.print(F(", bozuk cerceve"));
  }
  Serial.print(F(" | ECU cevap ")); Serial.print(answerCount);
  Serial.print(F(", jk ok/yok ")); Serial.print(jkOkCount);
  Serial.print('/'); Serial.println(jkFailCount);
#endif
}

// ---------------------------------------------------------------------------
static void answerBattery() {
  uint8_t buf[soco::BATTERY_FRAME_LEN];
  const uint8_t n = soco::buildBatteryResponse(currentStatus(), respDest, buf);

  digitalWrite(PIN_RS485_DIR, HIGH);      // MAX485 verici acik
  delayMicroseconds(20);
  bus.write(buf, n);
  bus.flushOutput();                      // son stop biti bitene kadar bekler
  delayMicroseconds(50);
  digitalWrite(PIN_RS485_DIR, LOW);       // tekrar dinle

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
//  LED: yavas yanip sonme = ECU sorgusu yok
//       hizli yanip sonme = ECU tamam, JK verisi yok (%100 gonderiliyor)
//       surekli yanik + kisa sonme = ECU ve JK tamam
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
  digitalWrite(PIN_RS485_DIR, LOW);       // dinle
  pinMode(PIN_LED, OUTPUT);

  pinMode(4, INPUT_PULLUP);               // JK TX -> D4
  pinMode(5, OUTPUT);                     // D5 -> JK RX
  digitalWrite(5, HIGH);                  // UART bos seviyesi

  Serial.begin(115200);
  bus.begin(SOCO_BAUD);                   // Timer1'i 16 MHz serbest sayaca alir
  pinMode(8, INPUT_PULLUP);

#if DEBUG_SERIAL
  Serial.println(F("JK_Soco_Final basladi. MAX485 D8/D9, JK D4/D5. JK yoksa 83V %100."));
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
