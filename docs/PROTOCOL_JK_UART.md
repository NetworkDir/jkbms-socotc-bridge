# JK BMS UART protokolü (`4E 57`)

Kaynak: [syssi/esphome-jk-bms](https://github.com/syssi/esphome-jk-bms)
(`docs/protocol-design-uart-ttl-gps-port.md`, `components/jk_modbus`, `components/jk_bms`).
Bu projede HW 11.XW / SW 11.272, 20S bir JK ile doğrulandı.

## Fiziksel katman

- 4 pinli JST 1.25 mm soket: `GND – RX – TX – VBAT` (bazı modellerde "GPS" veya "RS485" yazar)
- **3.3 V TTL**, **115200 baud, 8N1**
- VBAT = paket gerilimi (GPS modülünü beslemek için), bağlanmaz.
- JK sadece sorulunca cevap verir (slave).

## "Tüm veriyi oku" isteği (21 bayt)

```
4E 57 00 13 00 00 00 00 06 03 00 00 00 00 00 00 68 00 00 01 29
```

| Bayt | Değer | Anlamı |
|---|---|---|
| 0–1 | `4E 57` | Başlangıç ("NW") |
| 2–3 | `00 13` | Uzunluk = toplam bayt − 2 = 19 |
| 4–7 | `00 00 00 00` | Terminal no |
| 8 | `06` | Komut: tüm veriyi oku |
| 9 | `03` | Kaynak: 00 BMS, 01 Bluetooth, 02 GPS, 03 PC |
| 10 | `00` | Tip: 00 istek, 01 cevap, 02 aktif gönderim |
| 11 | `00` | Veri ID: 00 = hepsi |
| 12–15 | `00 00 00 00` | Kayıt no |
| 16 | `68` | Bitiş |
| 17–20 | `00 00 01 29` | Checksum: ilk 2 bayt 0, son 2 bayt = bayt 0…16 toplamı |

Kaynak `02` ile: `… 06 02 … 68 00 00 01 28`. Bu projede `03` ile cevap alındı.

Diğer komutlar: `01` aktivasyon, `02` yazma, `03` tek kayıt okuma, `05` şifre.
Ayar yazmak için önce şifre çerçevesi, ardından yazma çerçevesi gerekir; bu proje sadece okur.

## Cevap

Cevap `4E 57` ile başlar, `68 00 00 xx xx` ile biter. Hücre sayısına göre ~280–340 bayttır.

```
4E 57 LL LL 00 00 00 00 06 00 01 | veri: ID değer ID değer … | 00 00 00 00 68 00 00 CS CS
```

- Checksum: baştan sondaki 2 bayta kadar tüm baytların 16 bit toplamı = son 2 bayt.
- Veri alanı (bayt 11'den kayıt no'ya kadar) **ID + değer** çiftlerinden oluşur.

### Kullanılan alanlar

| ID | Bayt | Alan | Çözüm |
|---|---|---|---|
| `79` | 1 + 3n | Hücre gerilimleri | `79 <uzunluk>` sonra her hücre için `no, mV_H, mV_L`. 20 hücre → `79 3C` |
| `80` | 2 | MOSFET sıcaklığı | 0–100 = °C, 101–140 = −1…−40 °C |
| `81` | 2 | Sıcaklık T1 | aynı |
| `82` | 2 | Sıcaklık T2 | aynı |
| `83` | 2 | Toplam gerilim | 0.01 V |
| `84` | 2 | Akım | Protokol 1: bit15 = 1 şarj / 0 deşarj, kalan 15 bit 0.01 A. Protokol 0: `(10000 − değer) × 0.01 A` |
| `85` | 1 | SOC | % |
| `86` | 1 | Sıcaklık sensörü sayısı | |
| `87` | 2 | Döngü sayısı | |
| `89` | 4 | Toplam döngü kapasitesi | Ah |
| `8A` | 2 | Hücre sayısı | |
| `8B` | 2 | Alarmlar | aşağıya bak |
| `8C` | 2 | Durum | bit0 şarj MOS, bit1 deşarj MOS, bit2 balans |
| `C0` | 1 | Protokol sürümü | `84` akım formatını belirler |

Diğer ID'ler (8E…BA, ayarlar ve metinler) sabit uzunlukludur; çözümleyici bunları atlayarak ilerler
(`jk_bms.h` → `fieldLen()`).

Gerçek örnek (esphome-jk-bms dokümanından, 14 hücre):
`83 14 EF` = 53.59 V, `84 80 D0` = +2.08 A (şarj), `85 0F` = %15, `C0 01` = protokol 1.

### Alarm bitleri (`8B`)

| Bit | Alarm |
|---|---|
| 0 | Düşük kapasite |
| 1 | MOSFET aşırı sıcaklık |
| 2 | Şarj aşırı gerilim |
| 3 | Deşarj düşük gerilim |
| 4 | Batarya aşırı sıcaklık |
| 5 | Şarj aşırı akım |
| 6 | Deşarj aşırı akım |
| 7 | Hücre farkı |
| 8 | Kutu aşırı sıcaklık |
| 9 | Batarya düşük sıcaklık |
| 10 | Hücre aşırı gerilim |
| 11 | Hücre düşük gerilim |

## PC'den test

USB-TTL adaptör (veya RST pini GND'ye bağlı bir Nano) ile Hercules / RealTerm gibi bir HEX terminalden
yukarıdaki 21 baytı 115200'de gönder. Cevapta okunabilir metinler görünür: parola (`123456`),
üretim tarihi, yazılım sürümü (örn. `11.XW_S11.272`), üretici kaydı.
