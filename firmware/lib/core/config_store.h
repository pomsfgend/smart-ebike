#pragma once
// Хранение настроек в NVS (Preferences). Значения по умолчанию — include/config.h.
#include <stdint.h>

struct RuntimeConfig {
  float    battCapacityAh;
  uint8_t  battCells;
  float    autoLuxOn, autoLuxOff;
  uint8_t  underglowMode;      // см. underglow::Mode
  uint8_t  ledBrightness;      // 0..255, общий предел
};

namespace config_store {
void load();
void save();
RuntimeConfig& get();
}
