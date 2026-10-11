#pragma once
// Задний фонарь на адресной ленте: [левый поворот | центр: габарит/стоп | правый поворот].
// Приоритеты: стоп > габарит; поворот рисуется в своих сегментах поверх; аварийка — оба сегмента.
#include "vehicle_state.h"

namespace rear_light {
void init();
void render(const VehicleState& s, uint32_t nowMs);
}
