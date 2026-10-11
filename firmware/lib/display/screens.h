#pragma once
// Модель экранов (без Arduino/LVGL — тестируется на ПК). Рисование — в бэкенде (display_lvgl.cpp / лог).
#include <stdint.h>

namespace ui {

enum class Screen : uint8_t {
  Main = 0,    // крупно скорость, итоговый SoC, иконки свет/поворот/стоп/дальний, ошибки
  Battery,     // итог: V, I, P, SoC, остаток А·ч, Вт·ч/км, T шунта
  Packs,       // сборки A/B: SoC, I, V, ячейки min/max/Δ, T, MOSFET, защиты, online
  Lights,      // режим света OFF/AUTO/ON, lux, фара, режим днища, яркость
  Trip,        // км, время, ср. скорость, Вт·ч, Вт·ч/км; долгое «M» — сброс
  Charging,    // зарядка: ток, мощность, SoC, время до полного, T ячеек; появляется автоматически
  Count
};

enum class Nav : uint8_t { Up, Down, Select, Back };

struct NavState {
  Screen screen = Screen::Main;
  bool   autoCharging = false;   // экран зарядки открыт автоматически
};

// Переход по кнопкам и по событиям (начало зарядки, начало движения).
inline void navigate(NavState& st, Nav n) {
  uint8_t cnt = (uint8_t)Screen::Count, cur = (uint8_t)st.screen;
  switch (n) {
    case Nav::Down: case Nav::Select: st.screen = (Screen)((cur + 1) % cnt); break;
    case Nav::Up:   st.screen = (Screen)((cur + cnt - 1) % cnt); break;
    case Nav::Back: st.screen = Screen::Main; break;
  }
  st.autoCharging = false;
}

inline void autoSwitch(NavState& st, bool charging, float speedKmh) {
  if (charging && st.screen != Screen::Charging && !st.autoCharging) { st.screen = Screen::Charging; st.autoCharging = true; }
  if (!charging && st.autoCharging) { st.screen = Screen::Main; st.autoCharging = false; }
  if (speedKmh > 3 && st.screen == Screen::Charging) st.screen = Screen::Main;
}

// Яркость подсветки от освещённости: логарифмически между min и max (lux<0 — датчика нет → max).
inline uint8_t backlightPct(float lux, uint8_t minPct, uint8_t maxPct, float luxFull) {
  if (lux < 0) return maxPct;
  if (lux >= luxFull) return maxPct;
  if (lux <= 1) return minPct;
  // log10(1)=0 … log10(luxFull)
  float k = 0; float l = lux; float lf = luxFull; int d = 0, df = 0;
  while (l >= 10) { l /= 10; ++d; }
  while (lf >= 10) { lf /= 10; ++df; }
  k = (d + (l - 1) / 9.0f) / (df + (lf - 1) / 9.0f);   // грубый log10 без <cmath>
  if (k > 1) k = 1;
  return (uint8_t)(minPct + (maxPct - minPct) * k);
}

// Оценка времени до полного заряда, мин (−1 — неизвестно).
inline int minutesToFull(float socPct, float capacityAh, float chargeCurrentA) {
  if (chargeCurrentA < 0.5f) return -1;
  return (int)((100.0f - socPct) * 0.01f * capacityAh / chargeCurrentA * 60.0f);
}
}  // namespace ui
