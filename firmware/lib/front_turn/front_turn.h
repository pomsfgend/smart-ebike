#pragma once
// Передние электронные поворотники: две короткие ленты (левая/правая), мигание синхронно с задними.
// Опционально — режим «габарит» (тусклый янтарный/белый при включённом свете) — задача FRONT-03.
#include "vehicle_state.h"

namespace front_turn {
void init();
void render(const VehicleState& s, uint32_t nowMs);
}
