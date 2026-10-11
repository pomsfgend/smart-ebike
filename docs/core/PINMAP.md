# Карта выводов ESP32-S3-WROOM-1 N16R8

Источник истины — `firmware/include/pins.h`. **Все GPIO заняты**, дополнительные линии — через PCA9555 и ADS1115.

## GPIO
| GPIO | Функция | Напр. | Примечание |
|---|---|---|---|
| 1 | CONTACTOR_PWM | выход | LEDC ch2, 20 кГц → N-MOSFET катушки контактора |
| 2 | LDR / BTN_ADC | вход аналог. | фоторезистор ИЛИ ADC-лестница кнопок (взаимоисключающе) |
| 4 | CAN_TX | выход | SN65HVD230 |
| 5 | CAN_RX | вход | |
| 6 | TFT_SCK / BTN_GPIO_LIGHT_A | выход | по умолчанию — SPI дисплея |
| 7 | TFT_MOSI / BTN_GPIO_LIGHT_B | выход | |
| 8 | I2C_SDA | I/O | 4.7 кОм к 3.3 В |
| 9 | I2C_SCL | выход | 4.7 кОм |
| 10 | LED_UNDERGLOW | выход | RMT → 74AHCT125 |
| 11 | LED_REAR | выход | RMT |
| 12 | LED_FRONT_L | выход | RMT |
| 13 | LED_FRONT_R | выход | RMT |
| 14 | BRAKE_L | вход, LOW | оптопара; **пробуждение из deep sleep** |
| 15 | BRAKE_R | вход, LOW | оптопара |
| 16 | TFT_DC / BTN_GPIO_TURN_L | выход | |
| 17 | MC_UART_TX (UART1) | выход | к Fardriver/контроллеру мотора (уровни!) |
| 18 | MC_UART_RX (UART1) | вход | |
| 21 | TFT_CS / BTN_GPIO_TURN_R | выход | |
| 38 | BMS_B_TX (UART2) | выход | RGB-светодиод DevKitC-1 не использовать |
| 39 | TFT_BL / BTN_GPIO_HAZARD | выход | ШИМ подсветки (LovyanGFX ch7) |
| 40 | GRIP_PWM_L | выход | LEDC ch4, 200 Гц |
| 41 | HEADLIGHT_PWM | выход | LEDC ch0, 1 кГц |
| 42 | LED_5V_EN | выход | EN buck 12→5 В лент |
| 43 | BMS_A_TX (UART0) | выход | консоль — USB CDC |
| 44 | BMS_A_RX (UART0) | вход | |
| 47 | GRIP_PWM_R | выход | LEDC ch5 |
| 48 | BMS_B_RX (UART2) | вход | |
**Не использовать:** 0, 3, 45, 46 (strapping), 19/20 (USB), 26–32 (flash), 33–37 (октальная PSRAM).
Каналы LEDC: ch0 фара (таймер 0), ch2 катушка (таймер 1), ch4/ch5 подогрев (таймер 2), ch7 подсветка TFT (таймер 3) — разные частоты на разных таймерах.

## PCA9555 (I2C 0x20)
| Линия | Функция | Напр. |
|---|---|---|
| P0.0–P0.7 | кнопки руля 0–7 | вход, LOW = нажата |
| P1.0 | PRECHARGE_RELAY | выход |
| P1.1 | KILL_SENSE | вход, LOW = нажат |
| P1.2 | CONTACTOR_AUX | вход, LOW = замкнут |
| P1.3 | резерв | вход |
| P1.4 | GRIPS_PWR_EN | выход |
| P1.5 | резерв | вход |
| P1.6 | BUZZER (активный) | выход |
| P1.7 | HORN | выход |

## ADS1115 (I2C 0x48)
| Канал | Сигнал | Делитель |
|---|---|---|
| A0 | NTC ручки L | 10 кОм от 3.3 В |
| A1 | NTC ручки R | 10 кОм от 3.3 В |
| A2 | V_load (после контактора) | 300 кОм / 10 кОм (×31) |
| A3 | шина 12 В | 40 кОм / 10 кОм (×5) |

## I2C — адреса
INA228 `0x40`, PCA9555 `0x20`, ADS1115 `0x48`, BH1750 `0x23` (или VEML7700 `0x10`), OLED `0x3C` (если используется).
