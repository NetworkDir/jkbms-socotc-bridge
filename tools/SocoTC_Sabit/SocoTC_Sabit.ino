/*
  SocoTC_Sabit - Super Soco TC batarya emulatoru, SABIT degerler
  Kart: Arduino Nano + MAX485

  ECU'nun batarya sorgusuna (C5 5C 5A AA ...) her zaman:
     83 V, %100, 25 C, 0 A, hata yok   ile cevap verir.

  Baglanti (sniffer ile AYNI, calistigi kanitlanmis):
    MAX485 RO      -> D8
    MAX485 DI      -> D9
    MAX485 DE + RE -> D2   (iki pini kopru yap)
    MAX485 VCC/GND -> Nano 5V / GND
    MAX485 A / B   -> Soco RS485 A / B  (sniffer'da calisan yon)
    D0 / D1 BOS kalir -> kod yuklerken kablo cikarmak gerekmez.

  Gereken kutuphane: AltSoftSerial (Paul Stoffregen)

  Seri Monitor (115200): saniyede bir "ECU sorgu / cevap" sayilari yazar.
  LED (D13): her cevapta yanip soner.
*/

#include <AltSoftSerial.h>

// ------------------------- AYARLAR -------------------------
#define VOLTAGE_V     83     // gonderilen gerilim (V)
#define SOC_PERCENT   100    // gonderilen SOC (%)
#define TEMP_C        25     // gonderilen sicaklik (C)
#define CURRENT_A     0      // gonderilen akim (A)
#define CYCLES        0      // sarj dongusu

#define PIN_DIR       2      // MAX485 DE + RE
#define PIN_LED       13
#define RESPONSE_DELAY_US 1000UL   // sorgudan sonra cevap oncesi bekleme
// -----------------------------------------------------------

AltSoftSerial bus;           // RX = D8, TX = D9 (sabit)

static const uint32_t BYTE_US = 1042;     // 9600 baud'da 1 bayt

// Super Soco telegrami: T0 T1 HEDEF KAYNAK UZUNLUK VERI... CHECKSUM 0D
enum { ST_T0, ST_T1, ST_DEST, ST_SRC, ST_LEN, ST_DATA, ST_CHK, ST_END };
static uint8_t  st = ST_T0;
static uint8_t  t0, dest, src, len, idx, chk, calc;
static uint32_t lastByteMs = 0;

static uint8_t  frame[17];
static bool     answerPending = false;
static uint32_t answerAtUs = 0;
static bool     txActive = false;
static uint32_t txEndUs = 0;
static uint32_t lastAnswerMs = 0;

static uint32_t cntRequests = 0, cntAnswers = 0, cntBytes = 0;
static uint32_t lastPrintMs = 0;

static void startIfHeader(uint8_t b) {
  if (b == 0xC5 || b == 0xB6) { t0 = b; st = ST_T1; }
  else st = ST_T0;
}

// Gecerli bir BATARYA SORGUSU tamamlaninca true doner
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
  frame[0] = 0xB6; frame[1] = 0x6B; frame[2] = to; frame[3] = 0x5A; frame[4] = 10;
  frame[5]  = VOLTAGE_V;
  frame[6]  = SOC_PERCENT;
  frame[7]  = (uint8_t)(int8_t)TEMP_C;
  frame[8]  = (uint8_t)(int8_t)CURRENT_A;
  frame[9]  = (uint8_t)(CYCLES >> 8);
  frame[10] = (uint8_t)(CYCLES & 0xFF);
  frame[11] = (uint8_t)(CYCLES >> 8);    // desarj dongusu
  frame[12] = (uint8_t)(CYCLES & 0xFF);
  frame[13] = 0;                         // hata yok
  frame[14] = 0;                         // sarj olmuyor
  uint8_t c = frame[4];
  for (uint8_t i = 5; i < 15; i++) c ^= frame[i];
  frame[15] = c;
  frame[16] = 0x0D;
}

void setup() {
  pinMode(PIN_DIR, OUTPUT);
  digitalWrite(PIN_DIR, LOW);            // dinle
  pinMode(PIN_LED, OUTPUT);
  Serial.begin(115200);
  bus.begin(9600);
  pinMode(8, INPUT_PULLUP);
  Serial.println(F("SocoTC_Sabit basladi: 83V %100"));
}

void loop() {
  if (st != ST_T0 && millis() - lastByteMs > 20) st = ST_T0;   // yarim kalan telegrami at

  while (bus.available()) {
    const uint8_t b = (uint8_t)bus.read();
    lastByteMs = millis();
    cntBytes++;
    if (feed(b)) {
      cntRequests++;
      buildAnswer(src);
      answerPending = true;
      answerAtUs = micros() + RESPONSE_DELAY_US;
    }
  }

  // cevabi gonder (sniffer'daki calisan yontemin aynisi)
  if (answerPending && (int32_t)(micros() - answerAtUs) >= 0) {
    answerPending = false;
    digitalWrite(PIN_DIR, HIGH);
    delayMicroseconds(20);
    const uint32_t startUs = micros();
    bus.write(frame, sizeof(frame));
    txEndUs = startUs + sizeof(frame) * BYTE_US;
    txActive = true;
    lastAnswerMs = millis();
    cntAnswers++;
  }
  if (txActive && (int32_t)(micros() - (txEndUs + 1200)) >= 0) {
    digitalWrite(PIN_DIR, LOW);
    txActive = false;
  }

  // saniyede bir durum
  if (millis() - lastPrintMs >= 1000 && !txActive && !answerPending) {
    lastPrintMs = millis();
    Serial.print(F("hattan gelen bayt: "));  Serial.print(cntBytes);
    Serial.print(F(" | ECU batarya sorgusu: ")); Serial.print(cntRequests);
    Serial.print(F(" | cevap: "));           Serial.println(cntAnswers);
  }

  digitalWrite(PIN_LED, (millis() - lastAnswerMs) < 50 ? LOW : HIGH);
}
