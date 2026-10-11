# lib/buttons — кнопки на руле

| Файл | Назначение |
|---|---|
| `buttons_logic.h` | антидребезг, Short/Long/Repeat, типы Momentary/Level, декодер ADC-лестницы (чистый C++, тесты) |
| `buttons.*` | бэкенды GPIO / ADC / PCA9555, таблица назначений, действия → VehicleState и очередь UI |

Бэкенд — `CFG_BUTTONS_BACKEND` в `include/config.h`. Модель пульта пока неизвестна — таблица `kDefaultMap` типовая.
Документация: [`docs/buttons/`](../../../docs/buttons/README.md). Задачи: `BTN-*`.
