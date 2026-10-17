// ============================================================================
//  JK_Soco_Final - AYARLAR
// ============================================================================
#pragma once

// ---------------------------------------------------------------------------
//  Pinler (Arduino Nano)
//    MAX485      : RO -> D8, DI <- D9  (AltSoftSerial, sabit), DE+RE <- D2
//    JK BMS UART : JK TX -> D4, JK RX <- D5  (kodda port bitleri olarak sabit: PD4 / PD5)
// ---------------------------------------------------------------------------
#define PIN_RS485_DIR          2      // MAX485 DE + RE (kopru)
#define PIN_LED                13

// ---------------------------------------------------------------------------
//  Hizlar
// ---------------------------------------------------------------------------
#define SOCO_BAUD              9600UL     // ECU / gosterge / kontrolcu
#define JK_BAUD                115200UL   // JK BMS

// ---------------------------------------------------------------------------
//  Zamanlama  (sniffer olcumu: bizim cevaptan sonra ECU ~91 ms susuyor)
//  JK bu sessiz pencerede, kesmeler kapali olarak okunur.
// ---------------------------------------------------------------------------
#define RESPONSE_DELAY_US      1000UL   // ECU sorgusundan sonra cevaba baslamadan bekleme
#define JK_EVERY_N_CYCLES      3        // kac batarya cevabinda bir JK okunsun (3 = ~1 sn)
#define JK_RX_WINDOW_MS        60       // JK cevabi icin bekleme. 91 ms'yi gecmemeli!

// ---------------------------------------------------------------------------
//  JK BMS
// ---------------------------------------------------------------------------
#define JK_FRAME_SOURCE        0x03     // PC testinde JK bu istege cevap verdi (0x02 de denenebilir)
#define JK_DEFAULT_PROTOCOL    1
#define JK_STALE_MS            10000UL  // bu sure JK verisi gelmezse varsayilana don

// ---------------------------------------------------------------------------
//  JK verisi YOKKEN gonderilecek degerler (%100 gibi goster)
// ---------------------------------------------------------------------------
#define DEFAULT_VOLTAGE_cV     8300    // 83.00 V
#define DEFAULT_CURRENT_cA     0
#define DEFAULT_SOC            100     // %
#define DEFAULT_TEMP_C         25
#define DEFAULT_CYCLES         0

// ---------------------------------------------------------------------------
//  Donusum (convert.h)
// ---------------------------------------------------------------------------
#define TEMP_SOURCE            0       // 0=max(T1,T2) 1=T1 2=T2 3=max(T1,T2,MOS)
#define VOLTAGE_OFFSET_V       0
#define REPORT_ERRORS          1
#define REPORT_CHARGING_FLAG   1
#define CHARGE_DETECT_cA       100     // 1.00 A
#define CHARGE_RELEASE_cA      30
#define CHARGE_DETECT_MS       60000UL

// ---------------------------------------------------------------------------
//  Hata ayiklama: USB Seri Monitor (115200). D0/D1 bos, hicbir hatta karismaz.
// ---------------------------------------------------------------------------
#define DEBUG_SERIAL           1
