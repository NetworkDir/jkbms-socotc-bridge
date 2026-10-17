// ============================================================================
//  JK BMS UART (GPS port) protocol  -  "4E 57" frames, 115200 8N1
//
//  Source: syssi/esphome-jk-bms  docs/protocol-design-uart-ttl-gps-port.md
//          and components/jk_modbus, components/jk_bms
//
//  Frame:
//    [0..1]  4E 57          start
//    [2..3]  length (BE)    = total bytes - 2
//    [4..7]  terminal no
//    [8]     command        0x06 = read all data
//    [9]     source         0x00 BMS, 0x02 GPS, 0x03 PC
//    [10]    type           0x00 request, 0x01 response, 0x02 active upload
//    [11..]  data           sequence of (ID + value)
//    [n-9..n-6] record no
//    [n-5]   0x68           end
//    [n-4..n-1] checksum    first 2 bytes 0, last 2 bytes = sum of bytes [0..n-3]
//
//  This file does not depend on Arduino (it can be tested on a PC).
// ============================================================================
#pragma once
#include <stdint.h>
#include <string.h>

namespace jk {

static const uint16_t RX_BUF_SIZE = 400;   // enough even for 32 cells (~340 bytes)

// Data::present bits
enum : uint16_t {
  F_VOLT   = 1 << 0,
  F_CURR   = 1 << 1,
  F_SOC    = 1 << 2,
  F_TMOS   = 1 << 3,
  F_T1     = 1 << 4,
  F_T2     = 1 << 5,
  F_CYCLES = 1 << 6,
  F_ALARMS = 1 << 7,
  F_STATUS = 1 << 8,
  F_CELLS  = 1 << 9,
  F_PROTO  = 1 << 10,
};

// 0x8B alarm bits
enum : uint16_t {
  ALM_LOW_CAPACITY   = 1 << 0,
  ALM_MOS_OVERTEMP   = 1 << 1,
  ALM_CHARGE_OV      = 1 << 2,
  ALM_DISCHARGE_UV   = 1 << 3,
  ALM_BATT_OVERTEMP  = 1 << 4,
  ALM_CHARGE_OC      = 1 << 5,
  ALM_DISCHARGE_OC   = 1 << 6,
  ALM_CELL_DELTA     = 1 << 7,
  ALM_BOX_OVERTEMP   = 1 << 8,
  ALM_BATT_UNDERTEMP = 1 << 9,
  ALM_CELL_OV        = 1 << 10,
  ALM_CELL_UV        = 1 << 11,
};

struct Data {
  uint16_t present;        // which fields were found (F_ bits)
  uint16_t voltage_cV;     // total voltage, 0.01 V
  int32_t  current_cA;     // current, 0.01 A  (+ charge, - discharge)
  uint16_t rawCurrent;     // raw 0x84 value
  uint8_t  soc;            // %
  int16_t  tempMos;        // C
  int16_t  temp1;          // C (0x81)
  int16_t  temp2;          // C (0x82)
  uint8_t  tempSensors;    // 0x86
  uint16_t cycles;         // 0x87
  uint16_t alarms;         // 0x8B
  uint16_t status;         // 0x8C  bit0 charge MOS, bit1 discharge MOS, bit2 balancing
  uint8_t  cellCount;
  uint16_t cellMin_mV;
  uint16_t cellMax_mV;
  int16_t  protocol;       // 0xC0, -1 if not present
};

// ---------------------------------------------------------------------------
//  "Read all data" request (21 bytes). Example (source 0x03):
//  4E 57 00 13 00 00 00 00 06 03 00 00 00 00 00 00 68 00 00 01 29
// ---------------------------------------------------------------------------
static const uint8_t READ_ALL_LEN = 21;

inline uint8_t buildReadAllRequest(uint8_t *out, uint8_t frameSource) {
  const uint8_t f[17] = {
    0x4E, 0x57,             // start
    0x00, 0x13,             // length = 19
    0x00, 0x00, 0x00, 0x00, // terminal no
    0x06,                   // command: read all data
    frameSource,            // source
    0x00,                   // type: request
    0x00,                   // data ID: all
    0x00, 0x00, 0x00, 0x00, // record no
    0x68                    // end
  };
  uint16_t sum = 0;
  for (uint8_t i = 0; i < 17; i++) { out[i] = f[i]; sum += f[i]; }
  out[17] = 0x00;
  out[18] = 0x00;
  out[19] = (uint8_t)(sum >> 8);
  out[20] = (uint8_t)(sum & 0xFF);
  return READ_ALL_LEN;
}

// ---------------------------------------------------------------------------
//  Byte-by-byte frame receiver
// ---------------------------------------------------------------------------
class Receiver {
 public:
  enum Result : uint8_t { NONE = 0, FRAME_OK, FRAME_BAD };

  void reset() { len_ = 0; expected_ = 0; }
  bool busy() const { return len_ > 0; }

  // Call for every received byte. After FRAME_OK, frame()/frameLen() are valid.
  Result feed(uint8_t b) {
    if (len_ == 0) {
      if (b == 0x4E) buf_[len_++] = b;
      return NONE;
    }
    if (len_ == 1) {
      if (b == 0x57) { buf_[len_++] = b; }
      else { len_ = 0; if (b == 0x4E) buf_[len_++] = b; }
      return NONE;
    }
    buf_[len_++] = b;
    if (len_ == 4) {
      uint16_t l = ((uint16_t)buf_[2] << 8) | buf_[3];
      if (l < 19 || (uint32_t)l + 2 > RX_BUF_SIZE) { reset(); return FRAME_BAD; }
      expected_ = l + 2;
      return NONE;
    }
    if (len_ > 4 && len_ >= expected_) {
      frameLen_ = len_;
      reset();
      return checksumOk() ? FRAME_OK : FRAME_BAD;
    }
    return NONE;
  }

  const uint8_t *frame() const { return buf_; }
  uint16_t frameLen() const { return frameLen_; }

 private:
  bool checksumOk() const {
    uint16_t n = frameLen_;
    uint16_t sum = 0;
    for (uint16_t i = 0; i + 2 < n; i++) sum += buf_[i];
    uint16_t remote = ((uint16_t)buf_[n - 2] << 8) | buf_[n - 1];
    return sum == remote;
  }

  uint8_t  buf_[RX_BUF_SIZE];
  uint16_t len_ = 0;
  uint16_t expected_ = 0;
  uint16_t frameLen_ = 0;
};

// ---------------------------------------------------------------------------
//  Decoding helpers
// ---------------------------------------------------------------------------

// Byte length of each data ID (except 0x79, which has a variable length).
// -1 for an unknown ID.
inline int8_t fieldLen(uint8_t id) {
  switch (id) {
    case 0x85: case 0x86: case 0x9D: case 0xA9: case 0xAB: case 0xAC:
    case 0xAE: case 0xAF: case 0xB1: case 0xB3: case 0xB8: case 0xC0:
      return 1;
    case 0x89: case 0xAA: case 0xB5: case 0xB6: case 0xB9:
      return 4;
    case 0xB2: return 10;
    case 0xB4: return 8;
    case 0xB7: return 15;
    case 0xBA: return 24;
    default:
      if (id >= 0x80 && id <= 0x84) return 2;
      if (id == 0x87 || id == 0x8A || id == 0x8B || id == 0x8C) return 2;
      if (id >= 0x8E && id <= 0xA8) return 2;   // 0x9D is handled above (1 byte)
      if (id == 0xAD || id == 0xB0) return 2;
      return -1;
  }
}

// 0..100 = 0..100 C, 101..140 = -1..-40 C
inline int16_t decodeTemp(uint16_t raw) {
  return raw > 100 ? (int16_t)(100 - (int32_t)raw) : (int16_t)raw;
}

// Result in 0.01 A, + charge / - discharge
inline int32_t decodeCurrent(uint16_t raw, int16_t protocol) {
  // bit15 set means the new format for sure (old format values are ~0..20000)
  if (protocol == 1 || (raw & 0x8000)) {
    int32_t mag = raw & 0x7FFF;
    return (raw & 0x8000) ? mag : -mag;
  }
  // old format: 10000 = 0 A, 11000 = -10 A (discharge), 9500 = +5 A (charge)
  return 10000L - (int32_t)raw;
}

// ---------------------------------------------------------------------------
//  Decode a complete "read all" response. Returns true if voltage and SOC were found.
// ---------------------------------------------------------------------------
inline bool decode(const uint8_t *f, uint16_t n, Data &d, uint8_t defaultProtocol) {
  memset(&d, 0, sizeof(d));
  d.protocol = -1;

  if (n < 21 || f[0] != 0x4E || f[1] != 0x57) return false;
  if (f[8] != 0x06) return false;                  // command: read all
  if (f[10] != 0x01 && f[10] != 0x02) return false; // response or active upload
  if (f[n - 5] != 0x68) return false;

  const uint16_t end = n - 9;   // one past the last data byte (the record no starts here)
  uint16_t i = 11;

  while (i < end) {
    const uint8_t id = f[i];

    if (id == 0x79) {           // cell voltages: 79 <len> (no, mV_H, mV_L)*
      if (i + 1 >= end) break;
      const uint8_t blen = f[i + 1];
      if ((uint16_t)(i + 2 + blen) > end) break;
      uint8_t cells = blen / 3;
      uint16_t mn = 0xFFFF, mx = 0;
      for (uint8_t c = 0; c < cells; c++) {
        const uint8_t *p = &f[i + 2 + c * 3];
        uint16_t mv = ((uint16_t)p[1] << 8) | p[2];
        if (mv < mn) mn = mv;
        if (mv > mx) mx = mv;
      }
      d.cellCount = cells;
      if (cells) { d.cellMin_mV = mn; d.cellMax_mV = mx; d.present |= F_CELLS; }
      i += 2 + blen;
      continue;
    }

    const int8_t l = fieldLen(id);
    if (l < 0 || (uint16_t)(i + 1 + l) > end) break;   // unknown field: stop
    const uint8_t *v = &f[i + 1];
    const uint16_t v16 = (l >= 2) ? (((uint16_t)v[0] << 8) | v[1]) : v[0];

    switch (id) {
      case 0x80: d.tempMos = decodeTemp(v16); d.present |= F_TMOS; break;
      case 0x81: d.temp1   = decodeTemp(v16); d.present |= F_T1;   break;
      case 0x82: d.temp2   = decodeTemp(v16); d.present |= F_T2;   break;
      case 0x83: d.voltage_cV = v16;          d.present |= F_VOLT; break;
      case 0x84: d.rawCurrent = v16;          d.present |= F_CURR; break;
      case 0x85: d.soc = v[0];                d.present |= F_SOC;  break;
      case 0x86: d.tempSensors = v[0];                             break;
      case 0x87: d.cycles = v16;              d.present |= F_CYCLES; break;
      case 0x8B: d.alarms = v16;              d.present |= F_ALARMS; break;
      case 0x8C: d.status = v16;              d.present |= F_STATUS; break;
      case 0xC0: d.protocol = v[0];           d.present |= F_PROTO;  break;
      default: break;
    }
    i += 1 + l;
  }

  if (d.present & F_CURR) {
    int16_t proto = (d.present & F_PROTO) ? d.protocol : (int16_t)defaultProtocol;
    d.current_cA = decodeCurrent(d.rawCurrent, proto);
  }
  if (d.soc > 100) d.soc = 100;

  return (d.present & (F_VOLT | F_SOC)) == (F_VOLT | F_SOC);
}

}  // namespace jk
