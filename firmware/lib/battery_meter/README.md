# lib/battery_meter — счётчик заряда/разряда (18S2P, 2 × JBD BMS)

| Файл | Назначение |
|---|---|
| `ina228.*` | драйвер INA228 (шунт 0.375 мОм, ADCRANGE ±40.96 мВ, шина до 85 В) |
| `jbd_protocol.h` | кадры JBD BMS (0x03 базовая инфо, 0x04 ячейки), чистый C++ |
| `soc_estimator.h` | итоговый SoC: кулон-счётчик + коррекция по BMS + синхронизация 100 % |
| `battery_meter.*` | задача FreeRTOS: опрос, объединение, контроль разбаланса, NVS |

Вывод на OLED/BLE — задачи `BAT-06`, `BAT-07`. Документация: [`docs/battery-meter/`](../../../docs/battery-meter/README.md).
