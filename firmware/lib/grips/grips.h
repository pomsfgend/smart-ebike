#pragma once
// Подогрев ручек 12 В: 2 × logic-level N-MOSFET (GPIO40 / GPIO47, LEDC 200 Гц), NTC на ADS1115 (каналы 0/1),
// уровни 0..5 с кнопок/дисплея, автоотключение, отсечка по низкому заряду, бюджет тока шины 12 В.
#include <stdint.h>

namespace grips {
void init();
void task(void* arg);          // 200 мс
void setLevel(uint8_t level);  // обе ручки
void cycleLevel();             // 0→1→…→5→0
uint8_t level();
float tempC(uint8_t idx);
}
