#pragma once
// Логика подогрева ручек (чистый C++, тесты на ПК): уровень → целевая температура, ПИ-регулятор по NTC,
// ограничения: предел температуры, обрыв/КЗ датчика, бюджет тока 12 В, низкий заряд, таймер автоотключения.
#include <stdint.h>

namespace grips {

struct Params {
  uint8_t  levels;          // 5
  float    tLevel1C, tStepC, tMaxC;
  float    maxAEach;        // ток одной ручки при 100 %
  float    budgetA;         // суммарный бюджет на подогрев
  uint32_t autoOffMs;       // 0 — без таймера
  float    lowSocPct;
};

// NTC 10 кОм B3950 в делителе: 3.3 В — 10 кОм — [узел АЦП] — NTC — GND. Возвращает °C или −999 при обрыве/КЗ.
inline float ntcToC(float vAdc, float vRef = 3.3f, float rFixed = 10000, float r25 = 10000, float beta = 3950) {
  if (vAdc <= 0.02f || vAdc >= vRef - 0.02f) return -999;   // КЗ / обрыв
  float r = rFixed * vAdc / (vRef - vAdc);
  // Уравнение B: 1/T = 1/T25 + ln(R/R25)/B. ln(x) = 2·atanh((x−1)/(x+1)) — ряд из 6 членов,
  // без <cmath> (для тестов на ПК и МК одинаково); точность лучше 0.5 °C в диапазоне 0…70 °C.
  float x = r / r25, y = (x - 1) / (x + 1), y2 = y * y, ln = 0, term = y;
  for (int k = 1; k <= 11; k += 2) { ln += term / k; term *= y2; }
  ln *= 2;
  float invT = 1.0f / 298.15f + ln / beta;
  return 1.0f / invT - 273.15f;
}

class Controller {
 public:
  explicit Controller(const Params& p) : p_(p) {}

  void setLevel(uint8_t lvl, uint32_t now) { level_ = lvl > p_.levels ? p_.levels : lvl; since_ = now; integ_ = 0; }
  uint8_t level() const { return level_; }
  float target() const { return level_ ? p_.tLevel1C + (level_ - 1) * p_.tStepC : 0; }

  // Возвращает скважность 0..1. allowed = шина 12 В есть, нет запрета по SoC и т.п.
  // dutyCap — ограничение от менеджера бюджета (0..1).
  float update(float tempC, float socPct, bool railOn, float dutyCap, uint32_t now, float dtS) {
    if (level_ == 0) return 0;
    if (p_.autoOffMs && now - since_ >= p_.autoOffMs) { level_ = 0; return 0; }
    if (!railOn || socPct < p_.lowSocPct) return 0;
    if (tempC < -100 || tempC >= p_.tMaxC) { integ_ = 0; return 0; }   // нет датчика / перегрев → выкл
    float e = target() - tempC;
    integ_ += e * dtS * 0.002f;
    if (integ_ < 0) integ_ = 0;
    if (integ_ > 0.6f) integ_ = 0.6f;
    float d = 0.15f * e + integ_;
    if (d < 0) d = 0;
    if (d > 1) d = 1;
    if (d > dutyCap) d = dutyCap;
    return d;
  }

 private:
  Params   p_;
  uint8_t  level_ = 0;
  uint32_t since_ = 0;
  float    integ_ = 0;
};

// Ограничение скважности обеих ручек суммарным бюджетом тока.
inline float budgetDutyCap(float budgetA, float maxAEach, uint8_t activeGrips) {
  if (activeGrips == 0 || maxAEach <= 0) return 1;
  float cap = budgetA / (maxAEach * activeGrips);
  return cap > 1 ? 1 : cap;
}
}  // namespace grips
