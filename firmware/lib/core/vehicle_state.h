#pragma once
// Общее состояние велосипеда. Пишут задачи-«производители» (входы, батарея, автосвет),
// читают задачи-«потребители» (рендер лент, BLE, логгер). Доступ — под мьютексом,
// потребители работают с копией (snapshot), чтобы не держать блокировку.
#include <Arduino.h>

enum class LightSwitch : uint8_t { Off = 0, Auto = 1, On = 2 };
enum class TurnState   : uint8_t { None = 0, Left = 1, Right = 2, Hazard = 3 };

constexpr uint8_t kMaxPacks = 2;   // = CFG_BATT_PACKS_P
constexpr uint8_t kMaxCells = 24;

// Данные одной сборки из её JBD BMS.
struct PackData {
  bool     online      = false;   // BMS отвечает
  float    voltage_v   = 0;
  float    current_a   = 0;       // по данным BMS: >0 — разряд (знак приведён к общему соглашению)
  float    soc_pct     = 0;       // RSOC из BMS
  float    remain_ah   = 0;
  float    nominal_ah  = 0;
  uint16_t cycles      = 0;
  uint16_t protection  = 0;       // битовая маска защит JBD
  bool     chgFet = false, dsgFet = false;
  uint8_t  cellCount   = 0;
  uint16_t cell_mv[kMaxCells] = {};
  uint16_t cellMin_mv = 0, cellMax_mv = 0;
  uint8_t  ntcCount    = 0;
  float    temp_c[4]   = {};
  uint32_t lastUpdateMs = 0;
};

// Итог по батарее: шунт (INA228) + две BMS.
struct BatteryData {
  PackData packs[kMaxPacks];
  bool  shuntOk    = false;
  bool  packMismatch = false;    // разбаланс SoC/тока между сборками
  float voltage_v  = 0;
  float current_a  = 0;   // >0 — разряд, <0 — заряд/рекуперация
  float power_w    = 0;
  float soc_pct    = 0;   // итоговый SoC (кулон-счётчик + коррекция по BMS)
  float remain_ah  = 0;
  float energy_wh  = 0;   // накоплено с момента сброса
  float charge_ah  = 0;
  float temp_c     = 0;   // температура шунта/INA228 (кристалл)
  bool  charging   = false; // ток заряда > 1 А (в т.ч. быстрая зарядка из ветки charger-gbt)
  bool  valid      = false;
};

struct VehicleState {
  // Входы
  bool        brake        = false;
  TurnState   turn         = TurnState::None;
  uint32_t    turnSinceMs  = 0;     // момент включения поворота — общая фаза мигания для всех лент
  LightSwitch lightSwitch  = LightSwitch::Auto;
  float       ambientLux   = -1;    // <0 — датчик недоступен
  float       speedKmh     = 0;
  bool        horn         = false;
  // Поездка (сброс — длинное нажатие «меню» на экране «Поездка»)
  float       tripKm       = 0;
  float       tripWh       = 0;
  uint32_t    tripSeconds  = 0;
  // Выходы логики
  bool        mainLightOn  = false;
  // Батарея
  BatteryData battery;
};

namespace vstate {
void init();
VehicleState snapshot();
// Изменение под мьютексом: vstate::modify([](VehicleState& s){ s.brake = true; });
void lock();
void unlock();
VehicleState& raw();
template <typename F> inline void modify(F&& f) { lock(); f(raw()); unlock(); }

// Единая фаза мигания: передние, задние поворотники и днище мигают синхронно.
bool blinkOn(const VehicleState& s, uint32_t nowMs);
}  // namespace vstate
