# lib/power — главный контактор, предзаряд, kill, сон

`power_logic.h` — конечный автомат Off → Precharge → PullIn → On → Opening / Fault (чистый C++, тесты).
`power_ctrl.*` — ШИМ катушки (GPIO1, 20 кГц, компенсация по напряжению пакета + экономайзер), реле предзаряда и входы kill/aux через PCA9555, V_load через ADS1115, deep sleep с пробуждением тормозом.
Документация: [`docs/power/`](../../../docs/power/README.md). Задачи: `PWR-*`.
