# Super Soco TC RS485 protokolü

Kaynaklar: [Ldsrc2008/SocoTC-BMS-cheater](https://github.com/Ldsrc2008/SocoTC-BMS-cheater),
[stprograms/SuperSoco485Monitor](https://github.com/stprograms/SuperSoco485Monitor) ve bu projede
`tools/SocoTC_Sniffer` ile motosiklet üzerinde yapılan ölçümler. Protokol tersine mühendislikle
çözülmüştür; bazı alanların anlamı kesin değildir.

## Fiziksel katman

- RS485, iki tel (A/B), **9600 baud, 8N1**
- Master/slave: ECU sorar, diğerleri cevaplar. ECU yoksa gösterge master rolünü üstlenir.

## Telegram yapısı

| Bayt | 0 | 1 | 2 | 3 | 4 | 5 … 4+n | 5+n | 6+n |
|---|---|---|---|---|---|---|---|---|
| | T0 | T1 | Hedef | Kaynak | Uzunluk (n) | Veri | Checksum | `0D` |

- `C5 5C` = istek (master'dan), `B6 6B` = cevap
- **Checksum** = `n XOR veri[0] XOR … XOR veri[n-1]`
- `0D` bitiş baytıdır, ama veri içinde de geçebilir (örneğin 13 °C). Ayrıştırıcı uzunluk baytına göre çalışmalıdır.

## Adresler

| Adres | Cihaz |
|---|---|
| `AA` | ECU (master) |
| `BA` | Gösterge |
| `DA` | Motor kontrolcüsü |
| `5A` | Batarya (BMS) |

## Telegramlar

| Telegram | Bayt 2–3 | Veri uzunluğu | Örnek (motosikletten) |
|---|---|---|---|
| ECU → Gösterge | `BA AA` | 14 | `C5 5C BA AA 0E 50 00 00 00 08 00 00 00 01 01 00 00 00 70 26 0D` |
| Gösterge → ECU | `AA BA` | 1 | `B6 6B AA BA 01 00 01 0D` |
| ECU → Kontrolcü | `DA AA` | 2 | `C5 5C DA AA 02 00 00 02 0D` |
| Kontrolcü → ECU | `AA DA` | 10 | `B6 6B AA DA 0A 01 00 06 00 00 13 00 00 02 01 1D 0D` |
| ECU → Batarya | `5A AA` | 1 | `C5 5C 5A AA 01 97 96 0D` |
| Batarya → ECU | `AA 5A` | 10 | `B6 6B AA 5A 0A 42 4D 1B 00 00 05 00 03 00 00 18 0D` (orijinal BMS) |

## Batarya cevabı (veri, 10 bayt)

| Bayt | Alan | Tip | Açıklama |
|---|---|---|---|
| 0 | Gerilim | uint8 | Volt |
| 1 | SOC | uint8 | % |
| 2 | Sıcaklık | int8 | °C |
| 3 | Akım | int8 | A, negatif = deşarj |
| 4–5 | Şarj döngüsü | uint16 BE | |
| 6–7 | Deşarj döngüsü | uint16 BE | |
| 8 | Hata | bit alanı | aşağıya bak |
| 9 | Durum | uint8 | 1 = şarj oluyor, 0 = boşta (4 = deşarj, orijinal BMS'te gözlenmedi) |

Orijinal BMS örneği: `42 4D 1B 00 00 05 00 03 00 00` → 66 V, %77, 27 °C, 0 A, 5 şarj / 3 deşarj döngüsü.

### Hata baytı (bayt 8) → gösterge hata kodları

| Bit | Gösterge kodu | Anlamı |
|---|---|---|
| 0 | Err 92 | BMS şarjı kesti |
| 1 | Err 93 | Şarj akımı çok yüksek |
| 2 | Err 90 | Deşarj akımı çok yüksek |
| 5 | Err 89 | (bilinmiyor) |
| 7 | Err 88 | (bilinmiyor) |

Ayrıca: sıcaklık ≥ 60 °C → Err 91, batarya cevap vermezse → **Err 94**.

## Gösterge sorgusu (ECU → Gösterge, veri 14 bayt)

| Bayt | Alan |
|---|---|
| 0 | SOC (ECU'nun bataryadan aldığı son değer) |
| 1 | Kontrolcü akımı |
| 2 | Hız |
| 3 | Kontrolcü sıcaklık seviyesi |
| 4–5 | Saat / dakika |
| 6–7 | Hata kodları |
| 8 | Araç durumu (4 = şarj, 1 = park) |
| 9 | Vites |
| 10–11 | Kontrolcüden ham hız |
| 13 | Kalan menzil (km) |

Emülatör %80 gönderdiğinde ECU'nun göstergeye giden sorgusunda bayt 0 = `50` (80) görüldü. Yani ECU,
bataryadan aldığı SOC'u doğrudan göstergeye iletiyor.

## Motosiklette ölçülen zamanlama

`tools/SocoTC_Sniffer` ile ölçüldü (emülatör bataryaya 1 ms gecikmeyle cevap verirken):

```
t (ms)   boşluk    telegram
11570    +90.8     ECU→Batarya   sorgu   C5 5C 5A AA 01 97 96 0D
11579    + 2.7     BİZ→ECU       cevap   B6 6B AA 5A 0A 48 50 19 … 0B 0D
11688    +90.9     ECU→Gösterge  sorgu   C5 5C BA AA 0E …
11720    + 9.4     Gösterge→ECU  cevap   B6 6B AA BA 01 00 01 0D
11808    +79.9     ECU→Kontrolcü sorgu   C5 5C DA AA 02 00 00 02 0D
11819    + 0.7     Kontrolcü→ECU cevap   B6 6B AA DA 0A …
11928    +90.8     ECU→Batarya   sorgu   …
```

- ECU her **120 ms**'de bir sorgu gönderir; sıra gösterge → kontrolcü → batarya.
- Tam tur ≈ **358 ms**; batarya sorgusu da bu aralıkla gelir.
- Batarya cevabından sonra hat **~91 ms** boştur. JK bu aralıkta okunur.
- Batarya cevapları kesilince **Err 94** yaklaşık **3–4 sn** sonra gelir; tek tük kaçan sorgu sorun olmaz.
- Hatta kimse cevap vermezse ECU yine 120 ms sonra sıradaki cihaza geçer.
