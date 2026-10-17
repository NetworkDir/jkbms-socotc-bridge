// ============================================================================
//  Super Soco TC RS485 protocol  -  9600 8N1, master/slave
//
//  Sources: Ldsrc2008/SocoTC-BMS-cheater, stprograms/SuperSoco485Monitor
//
//  Telegram:  T0 T1 DEST SRC LEN DATA[LEN] CHK 0D
//    T0 T1   : C5 5C = request (from the master), B6 6B = response
//    address : AA = ECU (master), BA = dashboard, DA = motor controller, 5A = battery
//    CHK     : LEN ^ DATA[0] ^ ... ^ DATA[LEN-1]
//
//  Battery poll   : C5 5C 5A AA 01 97 96 0D
//  Battery answer : B6 6B AA 5A 0A  V SOC T I CYC_H CYC_L DCYC_H DCYC_L ERR CHG  CHK 0D
//
//  This file does not depend on Arduino (it can be tested on a PC).
// ============================================================================
#pragma once
#include <stdint.h>

namespace soco {

static const uint8_t REQ0 = 0xC5, REQ1 = 0x5C;
static const uint8_t RES0 = 0xB6, RES1 = 0x6B;
static const uint8_t END  = 0x0D;

static const uint8_t ADDR_ECU        = 0xAA;
static const uint8_t ADDR_SPEEDO     = 0xBA;
static const uint8_t ADDR_CONTROLLER = 0xDA;
static const uint8_t ADDR_BATTERY    = 0x5A;

static const uint8_t MAX_PDU = 32;
static const uint8_t BATTERY_PDU_LEN = 10;
static const uint8_t BATTERY_FRAME_LEN = 5 + BATTERY_PDU_LEN + 2;   // 17

// Error byte (byte 8) bits  -> error code shown on the dashboard
enum : uint8_t {
  ERR_CHARGE_STOPPED   = 1 << 0,   // Err 92: BMS stopped charging
  ERR_CHARGE_OC        = 1 << 1,   // Err 93: charge current too high
  ERR_DISCHARGE_OC     = 1 << 2,   // Err 90: discharge current too high
};

// Byte 9
enum : uint8_t {
  ACT_IDLE        = 0,
  ACT_CHARGING    = 1,
  ACT_DISCHARGING = 4,   // never seen from the original battery, not used
};

struct Telegram {
  uint8_t t0, t1, dest, src, len;
  uint8_t pdu[MAX_PDU];
};

struct BatteryStatus {
  uint8_t  voltage;          // V
  uint8_t  soc;              // %
  int8_t   temp;             // C
  int8_t   current;          // A, + charge / - discharge
  uint16_t cycles;           // charge cycles
  uint16_t dischargeCycles;  // discharge cycles
  uint8_t  error;            // ERR_ bits
  uint8_t  activity;         // ACT_
};

inline uint8_t xorChecksum(uint8_t len, const uint8_t *pdu) {
  uint8_t c = len;
  for (uint8_t i = 0; i < len; i++) c ^= pdu[i];
  return c;
}

// ---------------------------------------------------------------------------
//  Length-based telegram parser. (Treating 0x0D as the end of a telegram, as
//  the original project does, breaks when the value 13 shows up in the data,
//  e.g. 13 C.)
// ---------------------------------------------------------------------------
class Parser {
 public:
  void reset() { st_ = ST_T0; }
  bool busy() const { return st_ != ST_T0; }

  // Returns true when a valid telegram is complete; get it with telegram().
  bool feed(uint8_t b) {
    switch (st_) {
      case ST_T0:
        startIfHeader(b);
        return false;
      case ST_T1:
        if ((t_.t0 == REQ0 && b == REQ1) || (t_.t0 == RES0 && b == RES1)) {
          t_.t1 = b; st_ = ST_DEST;
        } else {
          st_ = ST_T0; startIfHeader(b);
        }
        return false;
      case ST_DEST: t_.dest = b; st_ = ST_SRC; return false;
      case ST_SRC:  t_.src  = b; st_ = ST_LEN; return false;
      case ST_LEN:
        if (b > MAX_PDU) { st_ = ST_T0; startIfHeader(b); return false; }
        t_.len = b; idx_ = 0; st_ = (b == 0) ? ST_CHK : ST_PDU;
        return false;
      case ST_PDU:
        t_.pdu[idx_++] = b;
        if (idx_ >= t_.len) st_ = ST_CHK;
        return false;
      case ST_CHK:
        chk_ = b; st_ = ST_END;
        return false;
      case ST_END:
        st_ = ST_T0;
        if (b == END && chk_ == xorChecksum(t_.len, t_.pdu)) return true;
        startIfHeader(b);
        return false;
    }
    st_ = ST_T0;
    return false;
  }

  const Telegram &telegram() const { return t_; }

 private:
  enum State : uint8_t { ST_T0, ST_T1, ST_DEST, ST_SRC, ST_LEN, ST_PDU, ST_CHK, ST_END };

  void startIfHeader(uint8_t b) {
    if (b == REQ0 || b == RES0) { t_.t0 = b; st_ = ST_T1; }
  }

  State   st_ = ST_T0;
  uint8_t idx_ = 0;
  uint8_t chk_ = 0;
  Telegram t_;
};

inline bool isRequestFor(const Telegram &t, uint8_t dest) {
  return t.t0 == REQ0 && t.t1 == REQ1 && t.dest == dest;
}

// ---------------------------------------------------------------------------
//  Build the battery answer (17 bytes). dest = address of the poller (AA).
// ---------------------------------------------------------------------------
inline uint8_t buildBatteryResponse(const BatteryStatus &s, uint8_t dest, uint8_t *out) {
  out[0] = RES0;
  out[1] = RES1;
  out[2] = dest;
  out[3] = ADDR_BATTERY;
  out[4] = BATTERY_PDU_LEN;
  uint8_t *p = &out[5];
  p[0] = s.voltage;
  p[1] = s.soc;
  p[2] = (uint8_t)s.temp;
  p[3] = (uint8_t)s.current;
  p[4] = (uint8_t)(s.cycles >> 8);
  p[5] = (uint8_t)(s.cycles & 0xFF);
  p[6] = (uint8_t)(s.dischargeCycles >> 8);
  p[7] = (uint8_t)(s.dischargeCycles & 0xFF);
  p[8] = s.error;
  p[9] = s.activity;
  out[15] = xorChecksum(BATTERY_PDU_LEN, p);
  out[16] = END;
  return BATTERY_FRAME_LEN;
}

}  // namespace soco
