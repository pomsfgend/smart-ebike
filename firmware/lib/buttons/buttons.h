#pragma once
// Модуль кнопок руля. Бэкенд выбирается CFG_BUTTONS_BACKEND (GPIO / ADC-лестница / PCA9555).
// Результат: поля VehicleState (turn, lightSwitch, horn, …) + очередь UI-событий для дисплея.
#include "buttons_logic.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace buttons {

constexpr uint8_t kMaxButtons = 12;

enum class UiEvent : uint8_t { Up, Down, Select, Back, LongSelect, TripReset };

void init();
void task(void* arg);            // опрос 5 мс
QueueHandle_t uiQueue();         // читает модуль display

// Таблица назначений по умолчанию (переопределяется из NVS после того, как станет известна модель кнопок).
const Mapping* mappingTable(uint8_t& count);
}  // namespace buttons
