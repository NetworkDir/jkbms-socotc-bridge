// ============================================================================
//  JK_Soco_Final - SETTINGS
// ============================================================================
#pragma once

// ---------------------------------------------------------------------------
//  Pins (Arduino Nano)
//    MAX485      : RO -> D8, DI <- D9  (AltSoftSerial, fixed), DE+RE <- D2
//    JK BMS UART : JK TX -> D4, JK RX <- D5  (hard-coded as port bits: PD4 / PD5)
// ---------------------------------------------------------------------------
#define PIN_RS485_DIR          2      // MAX485 DE + RE (bridged)
#define PIN_LED                13

// ---------------------------------------------------------------------------
//  Baud rates
// ---------------------------------------------------------------------------
#define SOCO_BAUD              9600UL     // ECU / dashboard / controller
#define JK_BAUD                115200UL   // JK BMS

// ---------------------------------------------------------------------------
//  Timing  (sniffer measurement: the ECU stays silent ~91 ms after our answer)
//  The JK is read inside this quiet window with interrupts disabled.
// ---------------------------------------------------------------------------
#define RESPONSE_DELAY_US      1000UL   // delay between the ECU poll and our answer
#define JK_EVERY_N_CYCLES      3        // read the JK every N battery answers (3 = ~1 s)
#define JK_RX_WINDOW_MS        60       // how long to wait for the JK reply. Must stay below 91 ms!

// ---------------------------------------------------------------------------
//  JK BMS
// ---------------------------------------------------------------------------
#define JK_FRAME_SOURCE        0x03     // the JK answered this in the PC test (0x02 can be tried too)
#define JK_DEFAULT_PROTOCOL    1
#define JK_STALE_MS            10000UL  // fall back to the defaults if no JK data for this long

// ---------------------------------------------------------------------------
//  Values sent while there is NO JK data (report a full battery)
// ---------------------------------------------------------------------------
#define DEFAULT_VOLTAGE_cV     8300    // 83.00 V
#define DEFAULT_CURRENT_cA     0
#define DEFAULT_SOC            100     // %
#define DEFAULT_TEMP_C         25
#define DEFAULT_CYCLES         0

// ---------------------------------------------------------------------------
//  Conversion (convert.h)
// ---------------------------------------------------------------------------
#define TEMP_SOURCE            0       // 0=max(T1,T2) 1=T1 2=T2 3=max(T1,T2,MOS)
#define VOLTAGE_OFFSET_V       0
#define REPORT_ERRORS          1
#define REPORT_CHARGING_FLAG   1
#define CHARGE_DETECT_cA       100     // 1.00 A
#define CHARGE_RELEASE_cA      30
#define CHARGE_DETECT_MS       60000UL

// ---------------------------------------------------------------------------
//  Debug: USB Serial Monitor (115200). D0/D1 are free, so it disturbs nothing.
// ---------------------------------------------------------------------------
#define DEBUG_SERIAL           1
