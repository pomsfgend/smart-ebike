#pragma once
// Входы, критичные для безопасности: датчики тормоза L/R (прямые GPIO, антидребезг 20 мс, опрос 5 мс).
// Кнопки руля (повороты, аварийка, свет, клаксон, меню) — модуль lib/buttons.
namespace inputs {
void init();
void task(void* arg);   // FreeRTOS-задача
}
