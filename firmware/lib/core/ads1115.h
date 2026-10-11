#pragma once
// Внешний 16-битный АЦП ADS1115 (0x48): NTC ручек, напряжение после контактора, шина 12 В. См. pins::adc.
#include <stdint.h>

namespace ads1115 {
bool init();
bool ok();
float readVolts(uint8_t channel);     // однократное преобразование, ±4.096 В, 250 SPS (~4 мс); <0 — ошибка
}
