#pragma once
// OTA-обновление по Wi‑Fi (ArduinoOTA, разделы app0/app1 — см. firmware/partitions.csv).
// Безопасность: OTA разрешено только на стоянке (скорость 0, ток < 2 А). Пароль — в NVS.
namespace ota {
void init();
void task(void* arg);
}
