# Аппаратная часть — заметки к схемам

Текстовые схемы и mermaid; KiCad-проект — на этапе v1.0 (задача CORE-09).

| Файл | Модуль |
|---|---|
| [core.md](core.md) | питание «мозга» (AON), ESP32-S3, CAN, тормоза, фара |
| [battery-meter.md](battery-meter.md) | шунт, INA228, изолированный UART к двум JBD BMS |
| [leds.md](leds.md) | ленты: днище, зад, перед; сдвиг уровня, питание |
| [auto-light.md](auto-light.md) | датчик освещённости, переключатель света |
| [power.md](power.md) | **силовая часть**: предохранители, контактор, предзаряд, kill, 6 AWG |
| [grips.md](grips.md) | подогрев ручек |
| [display.md](display.md) | SPI TFT 480×320 |
| [buttons.md](buttons.md) | кнопки руля: PCA9555 / ADC-лестница / GPIO |
