# JK BMS → Super Soco TC çevirici (Arduino Nano)

[![build](https://github.com/NetworkDir/jkbms-socotc-bridge/actions/workflows/build.yml/badge.svg)](https://github.com/NetworkDir/jkbms-socotc-bridge/actions/workflows/build.yml)

Super Soco TC'nin orijinal bataryası yerine **JK BMS'li bir paket** kullanırken motosikletin
ECU'sunu (beyin) "orijinal batarya takılı" diye kandıran Arduino Nano projesi.
Nano, JK BMS'i **UART** üzerinden okur ve ECU'nun **RS485** hattındaki batarya sorgularına
orijinal Soco BMS formatında cevap verir. JK'dan veri alınamazsa **83 V / %100** gönderir,
böylece motosiklet her durumda çalışır.

> **English summary** — Arduino Nano bridge that lets a Super Soco TC (RS485 bus, 9600 baud)
> run on a pack with a JK BMS. The Nano reads the JK BMS over its 4‑pin UART port
> (`4E 57` protocol, 115200 baud) and answers the ECU's battery polls (`C5 5C 5A AA …`) in
> the original Soco BMS format. If no JK data is available it reports 83 V / 100 %.
> Based on [Ldsrc2008/SocoTC-BMS-cheater](https://github.com/Ldsrc2008/SocoTC-BMS-cheater).
> Documentation is in Turkish; protocol notes are in [`docs/`](docs/).

---

## İçindekiler

- [Özellikler](#özellikler)
- [Gerekli parçalar](#gerekli-parçalar)
- [Bağlantı](#bağlantı)
- [Kurulum](#kurulum)
- [Nasıl çalışıyor](#nasıl-çalışıyor)
- [Veri eşleştirme](#veri-eşleştirme)
- [Ayarlar](#ayarlar-configh)
- [Seri Monitör ve LED](#seri-monitör-ve-led)
- [Yardımcı araçlar](#yardımcı-araçlar)
- [Klasör yapısı](#klasör-yapısı)
- [Bilinen sınırlamalar](#bilinen-sınırlamalar)
- [Kaynaklar](#kaynaklar)
- [Lisans](#lisans)

Ayrıntılı dokümanlar:

| Doküman | İçerik |
|---|---|
| [docs/WIRING.md](docs/WIRING.md) | Bağlantı şemaları, pinler, güvenlik notları |
| [docs/PROTOCOL_SOCOTC.md](docs/PROTOCOL_SOCOTC.md) | Super Soco RS485 protokolü ve motosiklette ölçülen zamanlamalar |
| [docs/PROTOCOL_JK_UART.md](docs/PROTOCOL_JK_UART.md) | JK BMS UART (`4E 57`) protokolü |
| [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | Sorun giderme ve geliştirme sırasında öğrenilenler |

---

## Özellikler

- ECU'nun batarya sorgusuna JK'nın **gerçek** değerleriyle cevap verir: gerilim, SOC, sıcaklık, akım, döngü sayısı, alarmlar.
- JK verisi yoksa veya 10 sn kesilirse **83 V / %100** gönderir. Motosiklet hiçbir zaman "batarya yok" (Err 94) hatasına düşmez.
- **D0/D1 kullanılmaz.** USB serbest kalır, Seri Monitör'den durum izlenebilir ve kod yüklerken kablo çıkarmak gerekmez.
- JK, ECU'nun sustuğu ~91 ms'lik aralıkta okunur; iki haberleşme birbirini bozmaz.
- JK UART'ı (115200) kütüphanesiz, kesmeler kapalıyken Timer1 sayacıyla bit bit sürülür. Simülasyonda JK'nın saati ±%2 saparken bile doğru okudu.
- Uzunluk tabanlı Soco ayrıştırıcı: veri içinde `0x0D` (örneğin 13 °C) geçse de bozulmaz.

## Gerekli parçalar

| Parça | Not |
|---|---|
| Arduino Nano (ATmega328P, 5 V) | Klon da olur (CH340) |
| MAX485 TTL ↔ RS485 modülü | DE ve RE pinleri köprülenecek |
| JK BMS | Klasik JK-B / JK-BD serisi, 4 pinli UART soketi olan (test: HW 11.XW, SW 11.272, 20S) |
| 4 pin JST 1.25 mm kablo | JK UART soketi için |
| (İsteğe bağlı) 1 kΩ + 2 kΩ direnç | Nano D5 → JK RX hattını 3.3 V'a indirmek için |

## Bağlantı

```
                 ┌────────────── Arduino Nano ──────────────┐
 MAX485 RO  ───▶ │ D8                                    D4 │ ◀─── JK TX
 MAX485 DI  ◀─── │ D9                                    D5 │ ───▶ JK RX
 MAX485 DE+RE ◀─ │ D2                                   GND │ ──── JK GND
 MAX485 VCC ◀─── │ 5V                                       │      JK VBAT → BOŞ!
 MAX485 GND ──── │ GND                                      │
                 └──────────────────────────────────────────┘
 MAX485 A / B ──── Super Soco RS485 A / B
```

| Nano | Bağlantı |
|---|---|
| D8 | MAX485 **RO** |
| D9 | MAX485 **DI** |
| D2 | MAX485 **DE + RE** (iki pin köprülü) |
| D4 | JK **TX** (4 pinli sokette VBAT'ın yanındaki pin) |
| D5 | JK **RX** (GND'nin yanındaki pin). İsteğe bağlı: 1 kΩ seri + JK RX'ten GND'ye 2 kΩ |
| GND | MAX485 GND + JK GND |
| 5V | MAX485 VCC |

**JK 4 pinli UART soketi:** `GND – RX – TX – VBAT`. VBAT pini **paket gerilimidir**, hiçbir yere bağlanmaz.
Bazı JK'larda bu soketin üzerinde "RS485" ya da "GPS" yazar ama içi 3.3 V TTL UART'tır.

**RS485 A/B:** Ters bağlanırsa hiçbir şey bozulmaz, sadece haberleşme olmaz. Sniffer aracıyla doğru yön kolayca bulunur
(bkz. [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md#a-ve-b-ters)).

> ⚠️ **Toprak notu:** JK'nın UART GND'si BMS'in **B-** tarafındadır, scooter'ın RS485 hattı ise **P-** tarafındadır.
> Nano ikisini birleştirir. JK koruma için deşarj MOSFET'lerini kestiğinde bu bağlantı MOSFET'leri köprüleyebilir.
> Ayrıntılar ve izolasyon önerisi (ADuM1201 + B0503S) için: [docs/WIRING.md](docs/WIRING.md#toprak-ve-izolasyon).

Tam şemalar ve beslemeyle ilgili notlar: [docs/WIRING.md](docs/WIRING.md)

## Kurulum

1. Arduino IDE'de **Araçlar → Kütüphaneleri Yönet** → **AltSoftSerial** (Paul Stoffregen) kur.
2. `JK_Soco_Final/JK_Soco_Final.ino` dosyasını aç. Klasördeki tüm `.h` dosyaları yanında kalmalı.
3. Kart: **Arduino Nano**, işlemci: **ATmega328P**. Yükleme hata verirse **ATmega328P (Old Bootloader)** seç.
4. Yükle. D0/D1 boş olduğu için kablo çıkarmaya gerek yok.
5. Seri Monitör'ü **115200** baud'da aç ve kontağı aç.

## Nasıl çalışıyor

```
ECU sorgu düzeni (motosiklette ölçüldü, her 120 ms'de bir sorgu, tam tur ~358 ms):

  ECU→Gösterge  ECU→Kontrolcü  ECU→Batarya ─ BİZ cevap ─┬─ [ ~91 ms sessizlik ] ─ ECU→Gösterge ...
                                                        └─ JK okuma burada (≤ 60 ms)
```

1. Nano RS485 hattını 9600 baud'da dinler (D8/D9, AltSoftSerial).
2. ECU'dan batarya sorgusu (`C5 5C 5A AA 01 97 96 0D`) gelince 1 ms sonra 17 baytlık cevabı gönderir.
3. İlk cevaptan hemen sonra, sonra her 3 cevapta bir (~1 sn), ECU'nun sustuğu aralıkta:
   - kesmeleri kapatır,
   - D5'ten JK'ya "tüm veriyi oku" isteğini gönderir (`4E 57 00 13 … 06 03 … 68 00 00 01 29`),
   - D4'ten JK'nın ~300 baytlık cevabını bit bit okur (en fazla 60 ms),
   - kesmeleri açar, cevabı checksum ile doğrular ve çözer.
4. Sonraki cevaplarda JK'nın değerleri kullanılır. JK verisi yoksa ya da 10 sn bayatsa 83 V / %100 gönderilir.

ECU, batarya cevapları kesilince ancak **3–4 saniye** sonra Err 94 veriyor; arada kaçan tek tük sorgu sorun olmaz.

## Veri eşleştirme

| Soco baytı | Anlamı | JK kaynağı | JK yokken |
|---|---|---|---|
| 0 | Gerilim (V) | `0x83` toplam gerilim, en yakın volta yuvarlanır | 83 |
| 1 | SOC (%) | `0x85` | 100 |
| 2 | Sıcaklık (°C, işaretli) | `0x81` / `0x82` (`TEMP_SOURCE`) | 25 |
| 3 | Akım (A, işaretli, + şarj) | `0x84`, −128…127 arası sınırlanır | 0 |
| 4–5 | Şarj döngüsü | `0x87` | 0 |
| 6–7 | Deşarj döngüsü | `0x87` (JK ayrı tutmuyor) | 0 |
| 8 | Hata bitleri | bit0 ← şarj/hücre aşırı gerilim (Err 92), bit1 ← şarj aşırı akım (Err 93), bit2 ← deşarj aşırı akım (Err 90) | 0 |
| 9 | Şarj oluyor | 1 = akım 60 sn boyunca ≥ 1 A | 0 |

## Ayarlar (`config.h`)

| Ayar | Varsayılan | Açıklama |
|---|---|---|
| `JK_EVERY_N_CYCLES` | 3 | Kaç batarya cevabında bir JK okunsun (3 ≈ 1 sn) |
| `JK_RX_WINDOW_MS` | 60 | JK cevabı için bekleme süresi. 91 ms'yi geçmemeli |
| `JK_FRAME_SOURCE` | 0x03 | İstek kaynak alanı. 0x03 test edildi; olmazsa 0x02 |
| `JK_STALE_MS` | 10000 | Bu süre JK verisi gelmezse varsayılan değerlere dön |
| `DEFAULT_VOLTAGE_cV` | 8300 | JK yokken gerilim (0.01 V) |
| `DEFAULT_SOC` | 100 | JK yokken SOC (%) |
| `DEFAULT_TEMP_C` | 25 | JK yokken sıcaklık |
| `TEMP_SOURCE` | 0 | 0 = max(T1,T2), 1 = T1, 2 = T2, 3 = max(T1,T2,MOSFET) |
| `VOLTAGE_OFFSET_V` | 0 | Gerilime eklenecek sabit ofset |
| `REPORT_ERRORS` | 1 | JK alarmlarını Soco hata baytına aktar |
| `REPORT_CHARGING_FLAG` | 1 | Uzun süreli şarj akımında "şarj oluyor" bildir |
| `CHARGE_DETECT_cA` / `_MS` | 100 / 60000 | Şarj sayılması için 1.00 A, 60 sn |
| `RESPONSE_DELAY_US` | 1000 | ECU sorgusundan sonra cevaba başlamadan bekleme |
| `DEBUG_SERIAL` | 1 | Seri Monitör'e durum satırı yaz |

## Seri Monitör ve LED

Seri Monitör (115200), her JK okumasında bir satır yazar:

```
JK OK  82.80V  0A  %97  25C  20 hucre | sure 32 ms | ECU cevap 4, jk ok/yok 2/0
JK YOK (83V %100 gonderiliyor)  gelen 0 bayt | ECU cevap 7, jk ok/yok 0/3
```

| LED (D13) | Anlamı |
|---|---|
| Sürekli yanık, her cevapta kısa söner | ECU ve JK tamam |
| Hızlı yanıp sönme (5 Hz) | ECU tamam, JK verisi yok (%100 gönderiliyor) |
| Yavaş yanıp sönme (1 Hz) | ECU'dan sorgu gelmiyor (A/B'yi kontrol et) |

## Yardımcı araçlar

| Araç | Ne işe yarar |
|---|---|
| [`tools/SocoTC_Sniffer`](tools/SocoTC_Sniffer/SocoTC_Sniffer.ino) | RS485 hattını dinler, her telegramı zaman damgası ve boşluk süresiyle yazar, 5 sn'de bir özet verir. İsteğe bağlı olarak bataryaya 72 V / %80 ile cevap verir. A/B yönünü bulmak ve zamanlamayı ölçmek için. Bağlantı ana projeyle aynı (D8/D9/D2). |
| [`tools/SocoTC_Sabit`](tools/SocoTC_Sabit/SocoTC_Sabit.ino) | JK'sız, en basit emülatör: her zaman 83 V / %100. Bağlantı aynı (D8/D9/D2). |
| [`tools/soco_master_sim.py`](tools/soco_master_sim.py) | PC'den USB-RS485 adaptörle ECU taklidi yapar, cevabı çözer (`pip install pyserial`). Motosiklet olmadan masada test için. |
| [`tests/host_test.cpp`](tests/host_test.cpp) | Protokol kodunun PC testleri: gerçek JK cevabı, orijinal Soco BMS cevabıyla birebir karşılaştırma, ayrıştırıcı. `cd tests && g++ -std=c++11 -I../JK_Soco_Final host_test.cpp -o host_test && ./host_test` |

## Klasör yapısı

```
├── JK_Soco_Final/          Ana Arduino projesi
│   ├── JK_Soco_Final.ino   Ana program (zamanlama, JK UART sürücüsü, RS485)
│   ├── config.h            Tüm ayarlar
│   ├── jk_bms.h            JK UART protokolü: istek, alıcı, çözümleyici
│   ├── soco_tc.h           Super Soco RS485 protokolü: ayrıştırıcı, cevap
│   └── convert.h           JK → Soco değer dönüşümü
├── tools/                  Sniffer, sabit emülatör, PC test aracı
├── tests/                  PC'de çalışan protokol testleri
└── docs/                   Protokol, bağlantı, sorun giderme dokümanları
```

## Bilinen sınırlamalar

- Soco protokolü tersine mühendislikle çözülmüştür. Bayt 6–7 (deşarj döngüsü) ve hata bitlerinin ECU üzerindeki tam etkisi kesin değildir.
- "Şarj oluyor" bayrağının ECU davranışına etkisi bilinmiyor. Rejeneratif frenleme yanlışlıkla şarj sayılmasın diye 60 sn şartı var; istenmezse `REPORT_CHARGING_FLAG 0`.
- Kod klasik JK-B / JK-BD serisinin `4E 57` UART protokolü içindir. JK-PB (inverter) serisi Modbus kullanır, desteklenmez.
- JK okunurken (~35–60 ms) `millis()` durur; bu sürede zamanlayıcıya bağlı başka iş yapılmaz.
- JK'nın kendi korumaları her durumda çalışmaya devam eder; çevirici sadece bilgi aktarır.

## Kaynaklar

- [Ldsrc2008/SocoTC-BMS-cheater](https://github.com/Ldsrc2008/SocoTC-BMS-cheater) — SocoTC batarya cevabı, ilk emülatör fikri
- [ldsrc.blogspot.com — Soco TC RS485 decode](https://ldsrc.blogspot.com/2023/12/soco-tc-rs485-decode.html)
- [stprograms/SuperSoco485Monitor](https://github.com/stprograms/SuperSoco485Monitor) — telegram yapısı, alan anlamları, hata kodları
- [syssi/esphome-jk-bms](https://github.com/syssi/esphome-jk-bms) — JK BMS UART protokolü ve örnek çerçeveler
- [PaulStoffregen/AltSoftSerial](https://github.com/PaulStoffregen/AltSoftSerial)

## Lisans

[MIT](LICENSE). Yüksek gerilimli bir batarya paketi ve motosiklet üzerinde çalışırsın; tüm risk
kullanana aittir. Bağlantıları yapmadan önce [docs/WIRING.md](docs/WIRING.md) içindeki toprak notunu oku.
