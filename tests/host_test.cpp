// PC uzerinde protokol mantigini test eder (Arduino gerekmez).
//   g++ -std=c++11 -Wall -Wextra -I../JK_Soco_Final host_test.cpp -o host_test && ./host_test
#include <cstdio>
#include <cstring>
#include <vector>
#include "convert.h"

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("HATA  satir %d: %s\n", __LINE__, #cond); fails++; } } while (0)

static std::vector<uint8_t> hex(const char *s) {
  std::vector<uint8_t> v;
  unsigned x;
  int n;
  while (sscanf(s, " %2x%n", &x, &n) == 1) { v.push_back((uint8_t)x); s += n; }
  return v;
}

int main() {
  // 1) JK istek cercevesi dokumandaki ornekle ayni mi (kaynak 0x03)
  {
    uint8_t req[21];
    jk::buildReadAllRequest(req, 0x03);
    auto ref = hex("4E 57 00 13 00 00 00 00 06 03 00 00 00 00 00 00 68 00 00 01 29");
    CHECK(memcmp(req, ref.data(), 21) == 0);
  }

  // 2) Dokumandaki gercek JK cevabi (14 hucre)
  auto frame = hex(
      "4E 57 01 1B 00 00 00 00 06 00 01 "
      "79 2A 01 0E ED 02 0E FA 03 0E F7 04 0E EC 05 0E F8 06 0E FA 07 0E F1 08 0E F8 "
      "09 0E E3 0A 0E FA 0B 0E F1 0C 0E FB 0D 0E FB 0E 0E F2 "
      "80 00 1D 81 00 1E 82 00 1C 83 14 EF 84 80 D0 85 0F 86 02 87 00 04 89 00 00 00 00 "
      "8A 00 0E 8B 00 00 8C 00 07 8E 16 26 8F 10 AE 90 0F D2 91 0F A0 92 00 05 93 0B EA "
      "94 0C 1C 95 00 05 96 01 2C 97 00 07 98 00 03 99 00 05 9A 00 05 9B 0C E4 9C 00 08 "
      "9D 01 9E 00 5A 9F 00 46 A0 00 64 A1 00 64 A2 00 14 A3 00 46 A4 00 46 A5 FF EC "
      "A6 FF F6 A7 FF EC A8 FF F6 A9 0E AA 00 00 00 0E AB 01 AC 01 AD 04 11 AE 01 AF 01 "
      "B0 00 0A B1 14 B2 31 32 33 34 35 36 00 00 00 00 B3 00 B4 49 6E 70 75 74 20 55 73 "
      "B5 32 31 30 31 B6 00 00 E2 00 B7 48 36 2E 58 5F 5F 53 36 2E 31 2E 33 53 5F 5F "
      "B8 00 B9 00 00 00 00 "
      "BA 42 54 33 30 37 32 30 32 30 31 32 30 30 30 30 32 30 30 35 32 31 30 30 31 "
      "C0 01 00 00 00 00 68 00 00 54 D1");
  CHECK(frame.size() == 0x011B + 2);

  static jk::Receiver rx;   // 400 bayt tampon
  jk::Data d;
  {
    // onunde cop veri + yarim baslik olsa da bulmali
    auto junk = hex("00 4E 13 4E 4E 57");
    int ok = 0;
    for (uint8_t b : junk) rx.feed(b);
    rx.reset();
    for (uint8_t b : frame) if (rx.feed(b) == jk::Receiver::FRAME_OK) ok++;
    CHECK(ok == 1);
    CHECK(jk::decode(rx.frame(), rx.frameLen(), d, 1));
    CHECK(d.voltage_cV == 5359);
    CHECK(d.current_cA == 208);          // +2.08 A sarj
    CHECK(d.soc == 15);
    CHECK(d.tempMos == 29 && d.temp1 == 30 && d.temp2 == 28);
    CHECK(d.cycles == 4);
    CHECK(d.alarms == 0 && d.status == 7);
    CHECK(d.cellCount == 14 && d.cellMin_mV == 0x0EE3 && d.cellMax_mV == 0x0EFB);
    CHECK(d.protocol == 1);
  }
  {
    // bozuk checksum reddedilmeli
    auto bad = frame;
    bad[20] ^= 0x01;
    rx.reset();
    int okc = 0, badc = 0;
    for (uint8_t b : bad) {
      auto r = rx.feed(b);
      if (r == jk::Receiver::FRAME_OK) okc++;
      if (r == jk::Receiver::FRAME_BAD) badc++;
    }
    CHECK(okc == 0 && badc == 1);
  }

  // 3) Sicaklik / akim cozumu
  CHECK(jk::decodeTemp(25) == 25);
  CHECK(jk::decodeTemp(101) == -1);
  CHECK(jk::decodeTemp(140) == -40);
  CHECK(jk::decodeCurrent(0x07D0, 1) == -2000);   // 20 A desarj
  CHECK(jk::decodeCurrent(0x87D0, 1) == 2000);    // 20 A sarj
  CHECK(jk::decodeCurrent(11000, 0) == -1000);    // eski format -10 A
  CHECK(jk::decodeCurrent(9500, 0) == 500);       // eski format +5 A

  // 4) JK -> Soco donusumu
  {
    soco::BatteryStatus s = conv::toSoco(d, false);
    CHECK(s.voltage == 54);   // 53.59 -> 54
    CHECK(s.soc == 15);
    CHECK(s.temp == 30);      // max(T1=30, T2=28)
    CHECK(s.current == 2);
    CHECK(s.cycles == 4 && s.dischargeCycles == 4);
    CHECK(s.error == 0 && s.activity == 0);

    jk::Data e = d;
    e.current_cA = -25060;    // -250.6 A -> -128'e sinirlanmali
    e.alarms = jk::ALM_DISCHARGE_OC | jk::ALM_CELL_OV;
    s = conv::toSoco(e, true);
    CHECK(s.current == -128);
    CHECK(s.error == (soco::ERR_DISCHARGE_OC | soco::ERR_CHARGE_STOPPED));
    CHECK(s.activity == soco::ACT_CHARGING);
    e.current_cA = -349;      // -3.49 -> -3
    CHECK(conv::toSoco(e, false).current == -3);
    e.current_cA = -351;      // -3.51 -> -4
    CHECK(conv::toSoco(e, false).current == -4);
  }

  // 5) Soco cevabi: orijinal BMS'ten yakalanan ornekle birebir
  {
    soco::BatteryStatus s = {0x42, 0x4D, 0x1B, 0, 5, 3, 0, 0};
    uint8_t out[17];
    CHECK(soco::buildBatteryResponse(s, soco::ADDR_ECU, out) == 17);
    auto ref = hex("B6 6B AA 5A 0A 42 4D 1B 00 00 05 00 03 00 00 18 0D");
    CHECK(memcmp(out, ref.data(), 17) == 0);
  }

  // 6) Soco ayristirici: gercek bus trafigi, araya cop ve 0x0D iceren cevap
  {
    auto bus = hex(
        "0D FF C5 "                                           // cop + yarim baslik
        "C5 5C DA AA 02 00 00 02 0D "                         // motor surucu sorgusu
        "B6 6B AA DA 0A 01 00 00 00 00 17 00 00 02 01 1F 0D " // motor surucu cevabi
        "B6 6B AA 5A 0A 42 4D 0D 00 00 05 00 03 00 00 0E 0D " // batarya cevabi (13 C!)
        "C5 5C 5A AA 01 97 96 0D "                            // BATARYA SORGUSU
        "C5 5C 5A AA 01 97 95 0D");                           // bozuk checksum
    soco::Parser p;
    int telegrams = 0, batteryReq = 0;
    for (uint8_t b : bus) {
      if (p.feed(b)) {
        telegrams++;
        if (soco::isRequestFor(p.telegram(), soco::ADDR_BATTERY)) {
          batteryReq++;
          CHECK(p.telegram().src == soco::ADDR_ECU);
        }
      }
    }
    CHECK(telegrams == 4);
    CHECK(batteryReq == 1);
  }

  // 7) Sarj algilama
  {
    conv::ChargeDetector cd;
    CHECK(!cd.update(500, 0));
    CHECK(!cd.update(500, CHARGE_DETECT_MS - 1));
    CHECK(cd.update(500, CHARGE_DETECT_MS));
    CHECK(cd.update(50, CHARGE_DETECT_MS + 10));    // histerezis bandi: devam
    CHECK(!cd.update(0, CHARGE_DETECT_MS + 20));
    CHECK(!cd.update(500, CHARGE_DETECT_MS + 30));  // tekrar beklemeli
  }

  printf(fails ? "\n%d test HATALI\n" : "Tum testler gecti\n", fails);
  return fails ? 1 : 0;
}
