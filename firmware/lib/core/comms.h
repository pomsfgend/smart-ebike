#pragma once
// Внешние шины: CAN (TWAI, 500 кбит/с) и UART1 к контроллеру мотора/дисплею.
// TODO(core-04): протокол конкретного контроллера мотора (KT/LCD3, Bafang, VESC CAN…) — уточнить у пользователя.
namespace comms {
void init();
void task(void* arg);
}
