#pragma once
// Автоматическое включение света: датчик освещённости (BH1750 0x23 / VEML7700 0x10 / LDR на GPIO2)
// + положение переключателя OFF/AUTO/ON. Управляет фарой (LEDC PWM) и флагом mainLightOn.
#include "auto_light_logic.h"

namespace auto_light {
void init();
void task(void* arg);   // период 100 мс
}
