# Модуль 1. Ядро контроллера ESP32-S3

## Назначение
Питание «мозга» (AON), шины (I2C, UART ×3, CAN, SPI), опрос тормозов, расширитель PCA9555 и АЦП ADS1115 (общие для модулей), общее состояние `VehicleState`, конфигурация (NVS), OTA, watchdog.

## Документы
- [PINMAP.md](PINMAP.md) — карта выводов
- [FREERTOS.md](FREERTOS.md) — задачи, приоритеты, ядра
- [POWER.md](POWER.md) — уровни питания: пакет / AON / 12 В
- Схемотехника: [../../hardware/core.md](../../hardware/core.md)
- Код: `firmware/lib/core/`, `firmware/src/main.cpp`, `firmware/include/`

## Шины
| Шина | Скорость | Устройства |
|---|---|---|
| I2C (GPIO8/9) | 400 кГц | INA228 `0x40`, PCA9555 `0x20`, ADS1115 `0x48`, BH1750 `0x23` (или VEML7700 `0x10`) |
| SPI (GPIO6/7/16/21) | 40 МГц | TFT 480×320 |
| UART0 (GPIO43/44) | 9600 | JBD BMS A (через изолятор) |
| UART1 (GPIO17/18) | уточнить | Fardriver ND72530 (протокол закрытый, описан сообществом — проверить) / текущий контроллер |
| UART2 (GPIO38/48) | 9600 | JBD BMS B (через изолятор) |
| TWAI/CAN (GPIO4/5) | 500 кбит/с | контроллер мотора с CAN (VESC и т. п.), резерв |
| USB CDC (GPIO19/20) | — | консоль, прошивка |

## Конфигурация
- Сборка: `firmware/include/config.h` (`CFG_*`), значения `[USER]` — уточнить.
- Время работы: NVS-пространство `ebike` (`config_store`), SoC — пространство `batt`.
- Планируется: BLE-сервис настроек и веб-страница в режиме точки доступа.

## OTA
- `ArduinoOTA` по Wi‑Fi, A/B-разделы (`firmware/partitions.csv`), откат при неудачной загрузке (`esp_ota_mark_app_valid_cancel_rollback` после самотеста — задача CORE-06).
- Только на стоянке; пароль OTA в NVS; Wi‑Fi выключается во время езды для экономии и снижения помех на RMT.

## Задачи
`CORE-01 … CORE-08` в [../TASKS.md](../TASKS.md).
