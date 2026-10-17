# Bağlantı

## Genel şema

```
  JK BMS                         Arduino Nano                        MAX485               Super Soco
 (4 pin UART)                                                                              RS485 hattı
 ┌─────────┐                ┌──────────────────────┐               ┌──────────┐
 │ GND     │────────────────│ GND              GND │───────────────│ GND      │
 │ RX      │◀──┬──[1k]──────│ D5               5V  │───────────────│ VCC      │
 │         │  [2k]          │                      │               │          │
 │         │   └─GND        │                  D8  │◀──────────────│ RO       │
 │ TX      │───────────────▶│ D4               D9  │──────────────▶│ DI       │
 │ VBAT    │  (BOŞ!)        │                  D2  │──────┬───────▶│ DE       │
 └─────────┘                │                  D13 │ LED  └───────▶│ RE       │
                            │  USB (D0/D1 boş)     │               │ A        │──── A
                            └──────────────────────┘               │ B        │──── B
                                                                   └──────────┘
```

`[1k]` / `[2k]` bölücü isteğe bağlıdır. JK'nın RX girişine 5 V yerine ~3.3 V gitmesini sağlar.
Testte bölücüsüz de çalıştı, ama uzun süreli kullanımda önerilir.

## Pin tablosu

| Nano pini | Bağlantı | Yön | Not |
|---|---|---|---|
| D2 | MAX485 DE + RE | çıkış | Yüksek = gönder, düşük = dinle |
| D4 | JK TX | giriş | Dahili pull-up açık |
| D5 | JK RX | çıkış | Boştayken yüksek |
| D8 | MAX485 RO | giriş | AltSoftSerial RX (sabit pin) |
| D9 | MAX485 DI | çıkış | AltSoftSerial TX (sabit pin) |
| D13 | Kart üzerindeki LED | çıkış | Durum göstergesi |
| D0 / D1 | — | — | **Kullanılmaz** (USB). JK'yı buraya bağlamayın, bkz. [TROUBLESHOOTING](TROUBLESHOOTING.md#jk-d0da-hiç-cevap-vermiyor) |
| D10 | — | — | AltSoftSerial nedeniyle PWM olarak kullanılamaz |

## JK BMS 4 pinli UART soketi

```
  ┌──────────────────────┐
  │  GND   RX   TX  VBAT │      JST 1.25 mm, 4 pin
  └──────────────────────┘
     │     │    │    └── paket gerilimi (örn. 84 V). BOŞ BIRAK!
     │     │    └─────── JK'nın çıkışı  → Nano D4
     │     └──────────── JK'nın girişi  ← Nano D5
     └────────────────── GND            → Nano GND
```

- Bazı JK modellerinde bu sokette "RS485" ya da "GPS" yazar, ama içi **3.3 V TTL UART**'tır. Gerçek RS485 için JK'nın ayrı RS485 adaptör kartı gerekir.
- **Pinleri ölçerken** siyah probu soketin kendi GND pinine koy. Doğru durumda VBAT = paket gerilimi, RX/TX ≈ 3.3 V çıkar. Siyah prob batarya eksisine ya da P-'ye değerse yanlış sonuç görebilirsin.
- 3 pinli soket (varsa) CAN veya ısıtıcı içindir, RS485/UART değildir. 6 pinli soket ekran ve düğme içindir.

## MAX485 modülü

| MAX485 | Bağlantı |
|---|---|
| VCC | Nano 5V |
| GND | Nano GND |
| RO | Nano D8 |
| DI | Nano D9 |
| DE + RE | Birbirine köprülü → Nano D2 |
| A / B | Super Soco RS485 A / B |

- **A/B yönü:** Ters bağlamak bir şey bozmaz, sadece haberleşme olmaz. Sniffer çıktısında `1D D4 …` ile başlayan "çöp" satırlar görüyorsan A ve B terstir.
- **120 Ω sonlandırma:** Modüllerin çoğunda R7 olarak bulunur. Haberleşme sorunu olursa sökmeyi dene.

## Super Soco tarafı

Super Soco'nun dahili RS485 hattında 4 cihaz vardır: ECU (`AA`), gösterge (`BA`), motor kontrolcüsü (`DA`) ve batarya (`5A`).
Orijinal bataryanın konnektöründeki A/B hatları kullanılır. TC Max'te koltuk altındaki 4 pinli JST SM konnektör de
aynı hatta açılır (pin 2 = B, pin 3 = A, pin 4 = GND; kaynak: SuperSoco485Monitor).

## Besleme

- Nano'yu scooter'ın **kontakla gelen 12 V** hattından besle: bir buck ile 5 V'a indirip 5V pinine ver, ya da VIN'e 7–12 V ver.
- Masada test için USB'den beslemek yeterli.
- JK soketindeki VBAT'ı Nano'yu beslemek için **kullanma** (paket gerilimi).

## Toprak ve izolasyon

JK BMS ortak portlu bir BMS'tir ve deşarj MOSFET'leri eksi taraftadır:

```
 Hücreler (−) ── B- ──[ JK MOSFET'leri ]── P- ── Scooter GND (ECU, RS485)
                  │
          JK UART GND
```

- JK'nın UART GND'si **B-** tarafındadır, scooter'ın RS485 referansı ise **P-** tarafındadır.
- Nano, JK GND'si ile scooter GND'sini birleştirir. Normal çalışmada B- ile P- arasında sadece MOSFET'lerin üzerindeki küçük gerilim düşümü vardır.
- **JK koruma için deşarj MOSFET'lerini kestiğinde** bu bağlantı MOSFET'leri Nano'nun GND hattı üzerinden köprüleyebilir. Bu durumda korumanın tam çalışmaması, Nano'nun GND hattından akım akması veya RS485 entegrelerinin zarar görmesi mümkündür.

Daha güvenli yapı için JK UART hattını izole et:

```
 Nano 5V ──▶ ADuM1201 VDD1          B0503S (5V → 3.3V izole) ──▶ ADuM1201 VDD2
 Nano GND ─▶ ADuM1201 GND1          B0503S 0V çıkış ───────────▶ ADuM1201 GND2 ──▶ JK GND
 Nano D5 ──▶ ADuM taraf-1 giriş  ══▶  ADuM taraf-2 çıkış ──▶ JK RX
 Nano D4 ◀── ADuM taraf-1 çıkış  ◀══  ADuM taraf-2 giriş ◀── JK TX
```

ADuM1201'in her tarafında bir giriş ve bir çıkış kanalı vardır; modül etiketinden giriş→çıkış yönünü kontrol et.
İzolatörün JK tarafı 3.3 V ile beslendiği için seviye dönüştürmeye de gerek kalmaz.
