#pragma once
// PCA9555 (0x20): порт 0 — кнопки руля, порт 1 — силовая часть (предзаряд, kill, aux, зуммер, клаксон).
// Номер линии: 0..7 = P0.x, 8..15 = P1.x (см. pins::xio).
#include <stdint.h>

namespace io_expander {
bool init();
bool ok();
bool readAll(uint16_t& in);           // оба порта, бит = уровень линии
bool write(uint8_t line, bool level); // только выходы порта 1
bool input(uint8_t line, bool& level);
}
