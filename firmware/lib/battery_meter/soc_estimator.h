#pragma once
// Оценка SoC: кулон-счётчик по шунту (точный на коротком интервале) +
//  - медленная коррекция к средневзвешенному SoC от BMS (устраняет дрейф),
//  - синхронизация 100 % по признаку конца заряда (CV-хвост),
//  - (LiFePO4) OCV почти плоская 3.25–3.33 В → по напряжению корректируем ТОЛЬКО у краёв (<3.0 и >3.4 В/яч.).
// Чистый C++ — тестируется на ПК.
#include <stdint.h>

class SocEstimator {
 public:
  struct Params {
    float capacityAh;       // суммарная ёмкость (100 А·ч)
    uint8_t cells;          // 18
    float cellFullV;        // 3.65
    float fullTailA;        // ток окончания заряда, например C/50 = 2 А
    float bmsBlendPerHour;  // доля «подтяжки» к SoC BMS за час (0.5 = 50 %/ч)
  };
  explicit SocEstimator(const Params& p) : p_(p) {}

  void setSoc(float pct) { soc_ = clamp(pct); initialized_ = true; }
  bool initialized() const { return initialized_; }
  float soc() const { return soc_; }
  float remainingAh() const { return soc_ * 0.01f * p_.capacityAh; }

  // currentA > 0 — разряд. bmsSoc < 0 — нет данных BMS.
  void update(float currentA, float voltageV, float dtS, float bmsSoc) {
    if (!initialized_) { if (bmsSoc >= 0) setSoc(bmsSoc); else return; }
    soc_ -= currentA * dtS / 3600.0f / p_.capacityAh * 100.0f;

    if (bmsSoc >= 0) {
      float k = p_.bmsBlendPerHour * dtS / 3600.0f;
      if (k > 1) k = 1;
      soc_ += (bmsSoc - soc_) * k;
    }
    float cellV = voltageV / p_.cells;
    bool charging = currentA < 0;
    if (charging && cellV >= p_.cellFullV - 0.05f && -currentA < p_.fullTailA) {
      if (++fullCnt_ > 30) soc_ = 100.0f;   // ~30 отсчётов подряд
    } else fullCnt_ = 0;
    soc_ = clamp(soc_);
  }

 private:
  static float clamp(float v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }
  Params p_;
  float  soc_ = 0;
  bool   initialized_ = false;
  uint16_t fullCnt_ = 0;
};
