#pragma once
// Модуль питания: главный контактор + предзаряд (Fardriver и прочие ВВ-потребители: DC-DC 12 В),
// kill-выключатель, режим сна «мозга». Мозг (ESP32-S3) питается всегда от отдельного малого источника.
#include "power_logic.h"

namespace power {
void init();
void task(void* arg);        // 10 мс
void requestOn(bool on);     // от кнопок/дисплея
bool loadOn();               // шина 12 В и контроллер мотора под напряжением
State state();
Fault fault();
}
