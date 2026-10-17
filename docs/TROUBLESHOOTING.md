# Sorun giderme

Bu notlar, proje geliştirilirken motosiklet üzerinde karşılaşılan gerçek sorunlardan çıkarıldı.

## Hızlı tablo

| Belirti | Sebep / çözüm |
|---|---|
| Seri Monitör'de `JK YOK … gelen 0 bayt` | JK cevap vermiyor: D4 ↔ D5 kablolarını değiştir, GND'yi kontrol et, `JK_FRAME_SOURCE`'u `0x02` dene, JK'yı uyandır (düğme veya Bluetooth) |
| `JK YOK` ama bayt sayısı > 0 | JK cevap veriyor ama çerçeve eksik/bozuk. Satırı kaydet; `JK_RX_WINDOW_MS`'yi 70–80 yapmayı dene (91'i geçme) |
| `ECU cevap` sayısı artmıyor, LED yavaş yanıp sönüyor | ECU sorgusu alınamıyor: A/B ters, RO → D8 bağlantısı, ortak GND |
| Sniffer'da `1D D4 …` ile başlayan çöp satırlar | [A ve B ters](#a-ve-b-ters) |
| Seri Monitör'de `NWh(` / `NWh)` | Nano'nun JK'ya gönderdiği isteğin kendisi (TX USB'ye de gidiyor), JK'nın cevabı değil |
| Göstergede Err 94 | Batarya cevapları ≥3–4 sn kesildi: MAX485 bağlantısı, A/B, Nano beslemesi |
| Göstergede Err 92 / 93 / 90 | JK'da gerçekten alarm var (uygulamaya bak) ya da `REPORT_ERRORS 0` |
| Kod yüklenmiyor | D0'a bir şey bağlıysa yükleme başarısız olur ve Nano'da **eski kod** çalışmaya devam eder. Bu projede D0 boş. Gerekirse "ATmega328P (Old Bootloader)" seç |

## A ve B ters

RS485'te A/B ters bağlanınca sinyal ters çevrilir. Nano yine bayt alır ama anlamsızdır. Bayt sayıları doğru
kalır, değerler yanlış olur. Motosiklette görülen örnek:

| Sniffer'da görünen | Gerçekte |
|---|---|
| `1D D4 6A A5 D1 9A 1E` (7 bayt) | ECU → batarya sorgusu `C5 5C 5A AA 01 97 96 0D` |
| `1D D4 64 65 …` (29 bayt) | Gösterge sorgusu (21) + cevabı (8) |
| `1D D4 62 65 FF FF FB E5 …` (26 bayt) | Kontrolcü sorgusu (9) + cevabı (17) |

Çözüm: MAX485'teki A ve B kablolarının yerini değiştir. Kodda değişiklik gerekmez.

## JK D0'da hiç cevap vermiyor

İlk denemelerde JK, Nano'nun donanım seri portuna (D0/D1) bağlıydı ve hiç veri gelmedi. Nano'nun TX'i
çalışıyordu (istek gidiyordu), ama D0'dan bir bayt bile okunamadı. Aynı JK'ya USB-TTL adaptörle
bilgisayardan aynı istek gönderilince JK hemen cevap verdi.

**Sebep:** Nano kartında USB çipinin (CH340) TX ucu D0'a 1 kΩ dirençle bağlıdır ve hattı sürekli 5 V'a çeker.
JK'nın 3.3 V'luk TX çıkışı büyük ihtimalle zayıf (seri dirençli) olduğu için bu çekmeyi yenemiyor ve ATmega328P
"0" bitini göremiyor. USB-TTL adaptörün girişinde böyle bir çekme olmadığı için orada çalışıyor.
Çoğu Nano klonunda USB çipi kart üzerindeki 5 V'tan beslendiğinden USB kablosu takılı olmasa da durum değişmez.

**Çözüm:** JK'yı D0 dışında bir pine bağlamak. Bu projede JK D4/D5'te.

Aynı sebeple MAX485 de D0/D1 yerine D8/D9'a bağlandı. D0/D1'deki denemeler başarısız olurken aynı MAX485
D8/D9'da (AltSoftSerial) ilk denemede çalıştı.

## Neden D4/D5'te kendi UART kodu?

- **AltSoftSerial** (D8/D9) 115200'ü güvenilir alır ama tek örnek açılabilir; MAX485 zaten orada.
- **SoftwareSerial** 9600'de sorunsuzdur, ama 115200'de JK'nın ~300 baytlık kesintisiz cevabında bayt kaçırır.
- Çözüm: JK, ECU'nun sustuğu ~91 ms'lik aralıkta okunuyor. Bu sürede kesmeler kapatılıp D4/D5 Timer1'in
  16 MHz sayacıyla bit bit sürülüyor (`jkSendByte`, `jkReceive`). Bit süresi 139 saat; örnekleme noktası
  bitin ortası. Simülasyonda JK saati ±%2 saparken bile doğru okundu.

## JK soketinde ölçüm

- Siyah probu **soketin kendi GND pinine** koy. Doğru sonuç: uçtaki bir pin paket gerilimi (VBAT), ortadaki iki pin ~3.3 V.
- Siyah prob batarya eksisine veya P-'ye değerse "tüm pinlerde batarya gerilimi var" gibi yanıltıcı sonuçlar görülebilir.
- Ortadaki pinlerde onlarca volt görüyorsan Nano'yu bağlama.

## JK'nın RS485'i

Bu tip JK'larda ayrı bir RS485 portu yoktur; 4 pinli soket TTL UART'tır. Gerçek RS485 için JK'nın
RS485 adaptör kartı gerekir. Adaptörle JK'yı scooter'ın RS485 hattına da bağlamak mümkündür
(ECU 9600, JK 115200; Nano ECU'nun sessiz aralığında baud değiştirir), ama bu projede UART yolu kullanıldı.

## Sniffer ile teşhis

`tools/SocoTC_Sniffer` ana projeyle aynı bağlantıyı kullanır (D8/D9/D2). Seri Monitör (115200):

```
11570 ms  +90.8 ms   ECU->Batarya   sorgu  C5 5C 5A AA 01 97 96 0D
11579 ms  +2.7 ms    BIZ->ECU  batarya cvp  B6 6B AA 5A 0A 48 50 19 … -> 72V %80 25C
=== OZET  tur=358.x/… ms | bat.cevap->bosluk=90.8/… ms | …
```

- `tur`: ECU'nun bir tam turu (≈358 ms)
- `bat.cevap->bosluk`: batarya cevabından sonra hattın boş kaldığı süre (JK okuma penceresi, ≈91 ms)
- `bat.cevapsiz bekleme`: kimse cevap vermezse ECU'nun bekleme süresi
- Hatta başka bir batarya/emülatör cevap veriyorsa sniffer'da `ANSWER_BATTERY 0` yap.
