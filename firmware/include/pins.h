#pragma once
// Карта выводов ESP32-S3 (модуль WROOM-1 N16R8). Подробности и обоснование: docs/core/PINMAP.md
// НЕ использовать: GPIO26–32 (SPI flash), GPIO33–37 (октальная PSRAM у R8),
// GPIO0/3/45/46 (strapping), GPIO19/20 (USB D-/D+),
// GPIO38/48 заняты под UART BMS-B → встроенный RGB-светодиод DevKitC-1 НЕ использовать.
// Консоль — USB CDC (ARDUINO_USB_CDC_ON_BOOT=1), поэтому аппаратный UART0 (GPIO43/44) свободен под BMS-A.
#include <stdint.h>

namespace pins {
// --- Аналоговые входы (ADC1, работает вместе с Wi‑Fi) ---
constexpr uint8_t CONTACTOR_PWM = 1; // драйвер катушки главного контактора (LEDC 20 кГц, экономайзер). Напряжение пакета меряет INA228
constexpr uint8_t LDR_SENSE    = 2;   // опционально: фоторезистор вместо BH1750/VEML7700

// --- Шины ---
constexpr uint8_t CAN_TX       = 4;   // TWAI → SN65HVD230 / TJA1051T/3
constexpr uint8_t CAN_RX       = 5;
constexpr uint8_t I2C_SDA      = 8;   // INA228, BH1750/VEML7700, OLED SSD1306
constexpr uint8_t I2C_SCL      = 9;
constexpr uint8_t MC_UART_TX   = 17;  // UART1 → контроллер мотора / дисплей (через уровни 3.3/5 В!)
constexpr uint8_t MC_UART_RX   = 18;
// JBD BMS — UART 9600 8N1, 3.3 В. ОБЯЗАТЕЛЬНА гальваноразвязка (ISO7721/ADuM1201): земля BMS = B-,
// а земля контроллера = P- (после силовых MOSFET BMS); при отключении BMS потенциалы расходятся.
constexpr uint8_t BMS_A_TX     = 43;  // UART0 (Serial0) → RX BMS сборки A
constexpr uint8_t BMS_A_RX     = 44;
constexpr uint8_t BMS_B_TX     = 38;  // UART2 (Serial2) → RX BMS сборки B
constexpr uint8_t BMS_B_RX     = 48;

// --- Адресные ленты (через 74AHCT125 → 5 В, резистор 330 Ом в линии) ---
constexpr uint8_t LED_UNDERGLOW = 10;
constexpr uint8_t LED_REAR      = 11;
constexpr uint8_t LED_FRONT_L   = 12;
constexpr uint8_t LED_FRONT_R   = 13;

// --- Тормоза: ВСЕГДА прямые GPIO (минимальная задержка, независимо от модуля кнопок) ---
constexpr uint8_t BRAKE_L      = 14;  // датчик ручки тормоза (геркон/Холл/концевик), оптопара, активный LOW
constexpr uint8_t BRAKE_R      = 15;
// INA228 ALERT не подключён (опрос по I2C) — GPIO47 отдан под подогрев ручек.

// --- Группа выводов 6/7/16/21/39: ЛИБО кнопки руля напрямую, ЛИБО SPI TFT (см. config.h) ---
// Вариант A — кнопки на GPIO (CFG_BUTTONS_BACKEND == BUTTONS_BACKEND_GPIO, дисплей не SPI):
constexpr uint8_t BTN_GPIO_TURN_L  = 16;
constexpr uint8_t BTN_GPIO_TURN_R  = 21;
constexpr uint8_t BTN_GPIO_LIGHT_A = 6;   // переключатель света AUTO (или кнопка «свет»)
constexpr uint8_t BTN_GPIO_LIGHT_B = 7;   // переключатель света ON (или кнопка «меню»)
constexpr uint8_t BTN_GPIO_HAZARD  = 39;
// Вариант B — SPI TFT 480×320 (ILI9488 3.5" по умолчанию / ST7796S 4") через GPIO-матрицу, до 40 МГц.
// MISO не подключается (у многих модулей ILI9488 SDO не уходит в Z-состояние). Тач (XPT2046) не используется — навигация кнопками.
constexpr uint8_t TFT_SCK      = 6;
constexpr uint8_t TFT_MOSI     = 7;
constexpr uint8_t TFT_DC       = 16;
constexpr uint8_t TFT_CS       = 21;
constexpr uint8_t TFT_BL       = 39;  // подсветка, LEDC PWM
// TFT_RST — к RC-сбросу/EN модуля или к выходу PCA9555 (свободных GPIO нет).

// --- Кнопки руля через ADC-лестницу (BUTTONS_BACKEND_ADC): GPIO2 (тогда LDR не используется) ---
constexpr uint8_t BTN_ADC      = LDR_SENSE;

// --- Выходы ---
constexpr uint8_t GRIP_PWM_L   = 40;  // подогрев левой ручки: logic-level N-MOSFET, шина 12 В (LEDC 200 Гц)
constexpr uint8_t GRIP_PWM_R   = 47;  // подогрев правой ручки
constexpr uint8_t HEADLIGHT_PWM = 41; // фара от шины 12 В (LEDC PWM → MOSFET)
constexpr uint8_t LED_5V_EN    = 42;  // EN преобразователя 12 → 5 В для лент

// --- Расширитель PCA9555 (I2C 0x20) — общий для кнопок и силовой части ---
// P0.0..P0.7 — кнопки руля (если CFG_BUTTONS_BACKEND == PCA9555)
namespace xio {
constexpr uint8_t PRECHARGE_RELAY = 8 + 0;  // P1.0 выход: реле/PhotoMOS предзаряда
constexpr uint8_t KILL_SENSE      = 8 + 1;  // P1.1 вход: аварийный выключатель (LOW = нажат/разомкнут)
constexpr uint8_t CONTACTOR_AUX   = 8 + 2;  // P1.2 вход: вспомогательный контакт контактора (LOW = замкнут)
constexpr uint8_t SPARE_IN_3      = 8 + 3;  // P1.3 вход: резерв
constexpr uint8_t GRIPS_PWR_EN    = 8 + 4;  // P1.4 выход: общий ключ питания подогрева (резервное отключение)
constexpr uint8_t SPARE_IN_5      = 8 + 5;  // P1.5 вход: резерв
constexpr uint8_t BUZZER          = 8 + 6;  // P1.6 выход: активный зуммер (тиканье поворотников)
constexpr uint8_t HORN            = 8 + 7;  // P1.7 выход: клаксон → MOSFET
constexpr uint8_t P1_DIR_MASK     = 0x2E;   // 1 = вход (P1.1, P1.2, P1.3, P1.5)
}

// --- Внешний АЦП ADS1115 (I2C 0x48) ---
namespace adc {
constexpr uint8_t GRIP_NTC_L = 0;   // NTC 10k B3950 левой ручки (делитель 10 кОм от 3.3 В)
constexpr uint8_t GRIP_NTC_R = 1;
constexpr uint8_t V_LOAD     = 2;   // напряжение на стороне контроллера мотора (после контактора), делитель 1:30
constexpr uint8_t V_12V      = 3;   // шина 12 В, делитель 1:5
}
}  // namespace pins
