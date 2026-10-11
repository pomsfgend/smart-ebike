// Минимальная конфигурация LVGL 8.3 для окружения esp32s3_lvgl. Остальное — значения по умолчанию lv_conf_internal.h.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH          16
#define LV_COLOR_16_SWAP        0
#define LV_MEM_CUSTOM           1          // malloc/free из IDF (PSRAM доступна через heap_caps)
#define LV_TICK_CUSTOM          1
#define LV_TICK_CUSTOM_INCLUDE  "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#define LV_DISP_DEF_REFR_PERIOD 50         // 20 Гц максимум; реально — по изменениям
#define LV_DPI_DEF              130

// Крупные шрифты для чтения на ходу
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_MONTSERRAT_28   1
#define LV_FONT_MONTSERRAT_48   1
#define LV_FONT_DEFAULT         &lv_font_montserrat_20
// Кириллица: подключить свой шрифт, сгенерированный lv_font_conv (диапазон 0x20-0x7F,0x401,0x410-0x44F,0x451) — DISP-04

#define LV_USE_LOG              0
#define LV_USE_PERF_MONITOR     0
#define LV_THEME_DEFAULT_DARK   1

#endif  // LV_CONF_H
