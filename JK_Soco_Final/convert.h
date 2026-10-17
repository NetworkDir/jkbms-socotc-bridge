// ============================================================================
//  JK BMS verisi  ->  SocoTC batarya cevabi
//
//  SocoTC byte | Anlam             | JK kaynagi
//  ------------+-------------------+-------------------------------------------
//   0          | Gerilim (V)       | 0x83 toplam gerilim, yuvarlanmis (+ofset)
//   1          | SOC (%)           | 0x85
//   2          | Sicaklik (C, isaretli) | 0x81/0x82 (/0x80) -> TEMP_SOURCE
//   3          | Akim (A, isaretli)| 0x84, + sarj / - desarj, -128..127 sinirli
//   4-5        | Sarj dongusu      | 0x87
//   6-7        | Desarj dongusu    | 0x87 (JK ayri tutmuyor)
//   8          | Hata              | 0x8B alarmlari (REPORT_ERRORS)
//   9          | Sarj oluyor       | uzun sureli sarj akimi (REPORT_CHARGING_FLAG)
// ============================================================================
#pragma once
#include "config.h"
#include "jk_bms.h"
#include "soco_tc.h"

namespace conv {

inline int32_t clampI32(int32_t v, int32_t lo, int32_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// 0.01 birimli degeri en yakin tam sayiya yuvarla (negatifler icin de dogru)
inline int32_t roundCenti(int32_t v) {
  return (v >= 0) ? (v + 50) / 100 : -((-v + 50) / 100);
}

inline int16_t pickTemperature(const jk::Data &d) {
  bool have = false;
  int16_t t = 0;
  const bool useT1  = (TEMP_SOURCE == 0 || TEMP_SOURCE == 1 || TEMP_SOURCE == 3);
  const bool useT2  = (TEMP_SOURCE == 0 || TEMP_SOURCE == 2 || TEMP_SOURCE == 3);
  const bool useMos = (TEMP_SOURCE == 3);
  if (useT1 && (d.present & jk::F_T1))      { t = d.temp1; have = true; }
  if (useT2 && (d.present & jk::F_T2))      { if (!have || d.temp2 > t) t = d.temp2; have = true; }
  if (useMos && (d.present & jk::F_TMOS))   { if (!have || d.tempMos > t) t = d.tempMos; have = true; }
  if (!have && (d.present & jk::F_TMOS))    { t = d.tempMos; have = true; }   // yedek
  return have ? t : 25;
}

inline uint8_t mapErrors(uint16_t alarms) {
  uint8_t e = 0;
  if (alarms & (jk::ALM_CHARGE_OV | jk::ALM_CELL_OV)) e |= soco::ERR_CHARGE_STOPPED;
  if (alarms & jk::ALM_CHARGE_OC)                     e |= soco::ERR_CHARGE_OC;
  if (alarms & jk::ALM_DISCHARGE_OC)                  e |= soco::ERR_DISCHARGE_OC;
  return e;
}

inline soco::BatteryStatus toSoco(const jk::Data &d, bool charging) {
  soco::BatteryStatus s;
  s.voltage  = (uint8_t)clampI32(roundCenti(d.voltage_cV) + VOLTAGE_OFFSET_V, 0, 255);
  s.soc      = (uint8_t)clampI32(d.soc, 0, 100);
  s.temp     = (int8_t)clampI32(pickTemperature(d), -40, 120);
  s.current  = (int8_t)clampI32(roundCenti(d.current_cA), -128, 127);
  s.cycles   = d.cycles;
  s.dischargeCycles = d.cycles;
  s.error    = REPORT_ERRORS ? mapErrors(d.alarms) : 0;
  s.activity = (REPORT_CHARGING_FLAG && charging) ? soco::ACT_CHARGING : soco::ACT_IDLE;
  return s;
}

// JK verisi yokken gonderilecek varsayilan degerler
inline void fillDefaults(jk::Data &d) {
  memset(&d, 0, sizeof(d));
  d.present    = jk::F_VOLT | jk::F_CURR | jk::F_SOC | jk::F_T1 | jk::F_T2 | jk::F_CYCLES;
  d.voltage_cV = DEFAULT_VOLTAGE_cV;
  d.current_cA = DEFAULT_CURRENT_cA;
  d.soc        = DEFAULT_SOC;
  d.temp1      = DEFAULT_TEMP_C;
  d.temp2      = DEFAULT_TEMP_C;
  d.cycles     = DEFAULT_CYCLES;
  d.protocol   = -1;
}

// Sarj algilama: akim CHARGE_DETECT_MS boyunca esigin uzerinde kalmali.
class ChargeDetector {
 public:
  bool update(int32_t current_cA, uint32_t nowMs) {
    if (current_cA >= CHARGE_DETECT_cA) {
      if (!timing_) { timing_ = true; since_ = nowMs; }
      if (nowMs - since_ >= CHARGE_DETECT_MS) charging_ = true;
    } else if (current_cA < CHARGE_RELEASE_cA) {
      timing_ = false;
      charging_ = false;
    }
    return charging_;
  }
  bool charging() const { return charging_; }

 private:
  bool timing_ = false;
  bool charging_ = false;
  uint32_t since_ = 0;
};

}  // namespace conv
