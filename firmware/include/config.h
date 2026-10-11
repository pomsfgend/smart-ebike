#pragma once
// Конфигурация сборки по умолчанию. Значения, помеченные [USER], нужно уточнить у владельца велосипеда.
// Во время работы часть параметров переопределяется из NVS (см. lib/core/config_store.h).
#include <stdint.h>

// ---------------- Батарея ----------------
// Данные пользователя: 18S2P, две сборки по 50 А·ч параллельно = 100 А·ч, полный заряд 65.7 В (3.65 В/яч.),
// номинал ~57.6 В. [ДОПУЩЕНИЕ — ПОДТВЕРДИТЬ] химия LiFePO4 (3.65 В max / 3.2 В номинал).
// У каждой сборки свой JBD BMS (UART), общий ток — через шунт + INA228.
#define CFG_BATT_CELLS_S          18
#define CFG_BATT_PACKS_P          2       // число параллельных сборок (= число BMS)
#define CFG_PACK_CAPACITY_AH      50.0f   // ёмкость одной сборки
#define CFG_BATT_CAPACITY_AH      (CFG_PACK_CAPACITY_AH * CFG_BATT_PACKS_P)  // 100 А·ч
#define CFG_CELL_V_FULL           3.65f   // LiFePO4 [ДОПУЩЕНИЕ]
#define CFG_CELL_V_NOMINAL        3.20f
#define CFG_CELL_V_EMPTY          2.80f   // программная отсечка «0 %», BMS отключит ниже (~2.5 В)
#define CFG_BATT_V_MAX            (CFG_CELL_V_FULL * CFG_BATT_CELLS_S)        // 65.7 В
// Шунт 75 мВ / 200 А (0.375 мОм): 100 А зарядки = 37.5 мВ → диапазон INA228 ±40.96 мВ (ADCRANGE=1).
#define CFG_SHUNT_MOHM            0.375f
#define CFG_SHUNT_MAX_A           109.0f  // 40.96 мВ / 0.375 мОм
#define CFG_INA228_ADCRANGE       1
#define CFG_BMS_POLL_MS           1000
#define CFG_PACK_SOC_MISMATCH_PCT 10.0f   // предупреждение о разбалансе сборок
#define CFG_PACK_CURRENT_SHARE_TOL 0.30f  // доля тока одной сборки должна быть 50 % ± 30 %

// ---------------- Светодиоды [USER] ----------------
#define CFG_LED_UNDERGLOW_COUNT   120     // длина ленты днища (левая + правая сторона)
#define CFG_UNDERGLOW_LEFT_START  0
#define CFG_UNDERGLOW_LEFT_COUNT  60
#define CFG_UNDERGLOW_RIGHT_START 60
#define CFG_UNDERGLOW_RIGHT_COUNT 60
#define CFG_LED_REAR_COUNT        30      // задний фонарь: [левый поворот | стоп/габарит | правый поворот]
#define CFG_REAR_TURN_SEG         10      // светодиодов на каждый сегмент поворота
#define CFG_LED_FRONT_COUNT       8       // на каждую сторону
#define CFG_LED_MAX_MA            3000    // ограничение тока лент через FastLED (5 В)
#define CFG_LED_FPS               60

// ---------------- Поворотники ----------------
// ГОСТ Р 41.48 / ECE R48: 90 ± 30 вспышек/мин. 1.5 Гц, скважность 50 %.
#define CFG_BLINK_PERIOD_MS       666
#define CFG_BLINK_ON_MS           333

// ---------------- Автосвет ----------------
#define CFG_AUTO_LUX_ON           40.0f   // ниже — включить свет
#define CFG_AUTO_LUX_OFF          120.0f  // выше — выключить (гистерезис)
#define CFG_AUTO_ON_DELAY_MS      2000    // быстро включаем (тоннель)
#define CFG_AUTO_OFF_DELAY_MS     15000   // медленно выключаем (тени, фонари)

// ---------------- Сеть / OTA ----------------
#define CFG_HOSTNAME              "smart-ebike"
#define CFG_OTA_ENABLED           1

// ---------------- Кнопки на руле [USER: модель и число кнопок не известны] ----------------
#define BUTTONS_BACKEND_GPIO      0   // каждая кнопка — свой GPIO (только если дисплей НЕ SPI)
#define BUTTONS_BACKEND_ADC       1   // резисторная лестница на одном АЦП (GPIO2, вместо LDR)
#define BUTTONS_BACKEND_PCA9555   2   // I2C-расширитель 16 линий на общей шине (по умолчанию)
#define CFG_BUTTONS_BACKEND       BUTTONS_BACKEND_PCA9555
#define CFG_PCA9555_ADDR          0x20
#define CFG_BTN_DEBOUNCE_MS       20
#define CFG_BTN_LONG_MS           800     // длинное нажатие
#define CFG_BTN_REPEAT_MS         250     // автоповтор при удержании (навигация меню)
#define CFG_TURN_AUTO_OFF_MS      20000   // автосброс поворота для кнопок без фиксации (0 — выкл)

// ---------------- Дисплей ----------------
// Пожелание пользователя: дёшево и крупно. По умолчанию — 3.5" ILI9488 480×320 SPI (самый дешёвый крупный).
// Альтернатива — 4.0" ST7796S 480×320 SPI (16 бит/пиксель по SPI → быстрее ILI9488, у которого по SPI только 18 бит).
// Путь развития — 4.3"/5" 800×480 параллельный RGB (ESP32-S3 RGB LCD), но нужно ~16–20 GPIO → только на своей плате.
#define DISPLAY_NONE              0
#define DISPLAY_ILI9488_SPI       1   // 3.5" 480×320, RGB666 по SPI (3 байта/пиксель)
#define DISPLAY_ST7796S_SPI       2   // 4.0" 480×320, RGB565 по SPI
#define DISPLAY_I2C_OLED          3   // SSD1306/SH1106 128×64 (минимальный вариант)
#define CFG_DISPLAY_TYPE          DISPLAY_ILI9488_SPI
#define CFG_DISPLAY_IS_SPI        (CFG_DISPLAY_TYPE == DISPLAY_ILI9488_SPI || CFG_DISPLAY_TYPE == DISPLAY_ST7796S_SPI)
#define CFG_DISPLAY_W             480     // альбомная ориентация
#define CFG_DISPLAY_H             320
#define CFG_DISPLAY_ROTATION      1
#define CFG_DISPLAY_SPI_HZ        40000000  // GPIO-матрица ESP32-S3: до 40 МГц; ILI9488 обычно стабилен на 27–40 МГц
#define CFG_DISPLAY_FPS           10        // интерфейс; LVGL перерисовывает только изменённые области
#define CFG_DISPLAY_BUF_LINES     40        // 2 буфера 480×40 во внутренней RAM с DMA
// Подсветка: PWM, яркость от датчика освещённости (днём 100 %, ночью ~15 %) — читаемость на солнце.
#define CFG_BL_MIN_PCT            15
#define CFG_BL_MAX_PCT            100
#define CFG_BL_LUX_FULL           5000.0f   // при этом lux и выше — 100 %

#if (CFG_BUTTONS_BACKEND == BUTTONS_BACKEND_GPIO) && CFG_DISPLAY_IS_SPI
#error "Кнопки на прямых GPIO и SPI TFT делят выводы 6/7/16/21/39 — выберите PCA9555/ADC для кнопок или OLED"
#endif

// ---------------- Питание [USER/ДОПУЩЕНИЯ] ----------------
// Мозг (ESP32-S3) питается ВСЕГДА: пакет → предохранитель 1 А → LM5164 (100 В) → 12V_AON → 3.3 В.
// Шина 12 В для нагрузок — имеющийся у пользователя DC-DC (без маркировки, ~20–30 А, пик 35 А измерен),
// стоит ПОСЛЕ главного контактора. [ДОПУЩЕНИЕ] длительный бюджет 20 А.
#define CFG_12V_BUDGET_A          20.0f
#define CFG_12V_HEADLIGHT_A       2.0f    // [USER] уточнить фару
#define CFG_12V_LEDS_A            2.5f    // 5 В / 5 А через buck 12→5 В (КПД ~90 %)

// ---------------- Главный контактор / предзаряд ----------------
#define CFG_PRECHARGE_MIN_MS      200
#define CFG_PRECHARGE_TIMEOUT_MS  3000    // не дошли до порога → КЗ/неисправность нагрузки
#define CFG_PRECHARGE_RATIO       0.92f   // V_load / V_pack для замыкания контактора
// Катушка 12 В питается от пакета (+ через kill-выключатель и предохранитель 2 А) низкосторонним MOSFET с ШИМ:
// скважность компенсируется по напряжению пакета (≈ 12 / Vpack), затем удержание (экономайзер).
#define CFG_COIL_NOMINAL_V        12.0f
#define CFG_CONTACTOR_PULLIN_MS   150     // втягивание: эквивалент 12 В на катушке
#define CFG_CONTACTOR_HOLD_PCT    35      // удержание: % от втягивания (уточнить по даташиту контактора)
#define CFG_CONTACTOR_PWM_HZ      20000
// Контроллер мотора: сейчас ~80 А; план — Fardriver ND72530 (48/60/72 В, пик 88 В; DC до 350 А, фаза 530 А),
// пользователь ограничит 100 А длительно / 120 А пик (2 × JBD 60/70 А). [USER] подтвердить настройки Fardriver.
#define CFG_MC_BATT_CONT_A        100.0f
#define CFG_MC_BATT_PEAK_A        120.0f
#define CFG_CONTACTOR_OPEN_MAX_A  10.0f   // штатное размыкание только при токе ниже (кроме аварии)
#define CFG_CONTACTOR_HAS_AUX     1       // [USER] есть ли у контактора вспомогательный контакт
#define CFG_AUX_TIMEOUT_MS        200     // подтверждение по вспомогательному контакту
#define CFG_SLEEP_AFTER_OFF_MS    600000  // 10 мин после выключения → deep sleep (пробуждение тормозом/кнопкой)

// ---------------- Подогрев ручек ----------------
#define CFG_GRIP_LEVELS           5       // 0 = выкл, 1..5
#define CFG_GRIP_T_LEVEL1_C       30.0f   // цель уровня n = T1 + (n-1)*STEP
#define CFG_GRIP_T_STEP_C         5.0f    // 30/35/40/45/50 °C
#define CFG_GRIP_T_MAX_C          55.0f   // жёсткий предел (ожог/клей ручки)
#define CFG_GRIP_MAX_A_EACH       2.5f    // [USER] ток одной ручки при 100 % (12 В) — уточнить
#define CFG_GRIP_BUDGET_A         5.0f    // доля бюджета 12 В на подогрев
#define CFG_GRIP_AUTO_OFF_MIN     30      // автоотключение, мин (0 — выкл)
#define CFG_GRIP_LOW_SOC_PCT      15.0f   // ниже — подогрев отключается
#define CFG_GRIP_PWM_HZ           200
