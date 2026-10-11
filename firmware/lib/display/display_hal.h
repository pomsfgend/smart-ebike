#pragma once
// Слой абстракции дисплея. Реализации:
//  • LvglDisplay   — LVGL 8.3 + LovyanGFX (ILI9488 / ST7796S по SPI; RGB-панели — на своей плате), сборка с -DUSE_LVGL;
//  • LogDisplay    — заглушка: печатает экран в консоль 1 раз/с (по умолчанию, пока дисплей не выбран окончательно);
//  • (план) OledDisplay — SSD1306/SH1106 128×64 (U8g2).
#include "screens.h"
#include "vehicle_state.h"

class DisplayHal {
 public:
  virtual ~DisplayHal() = default;
  virtual bool begin() = 0;
  virtual void setBacklight(uint8_t pct) = 0;
  // Отрисовать экран по снимку состояния. Вызывается с частотой CFG_DISPLAY_FPS.
  virtual void render(ui::Screen screen, const VehicleState& s, uint32_t nowMs) = 0;
  virtual const char* name() const = 0;
};

DisplayHal& displayBackend();   // выбор по CFG_DISPLAY_TYPE и USE_LVGL
