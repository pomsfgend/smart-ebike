#include "display.h"
#include "buttons.h"
#include "config.h"
#include "display_hal.h"
#include "screens.h"
#include "vehicle_state.h"
#include <Arduino.h>

namespace {
ui::NavState g_nav;
DisplayHal* g_hal = nullptr;

void handleUi(buttons::UiEvent e) {
  switch (e) {
    case buttons::UiEvent::Up:     ui::navigate(g_nav, ui::Nav::Up); break;
    case buttons::UiEvent::Down:   ui::navigate(g_nav, ui::Nav::Down); break;
    case buttons::UiEvent::Select: ui::navigate(g_nav, ui::Nav::Select); break;
    case buttons::UiEvent::Back:   ui::navigate(g_nav, ui::Nav::Back); break;
    case buttons::UiEvent::LongSelect: break;
    case buttons::UiEvent::TripReset:
      vstate::modify([](VehicleState& s) { s.tripKm = 0; s.tripWh = 0; s.tripSeconds = 0; });
      break;
  }
}
}  // namespace

namespace display {
void init() {
  g_hal = &displayBackend();
  bool ok = g_hal->begin();
  log_i("display backend '%s' %s", g_hal->name(), ok ? "ok" : "FAILED");
}

void task(void*) {
  TickType_t last = xTaskGetTickCount();
  const uint32_t periodMs = 1000 / CFG_DISPLAY_FPS;
  uint32_t lastTick = millis();
  for (;;) {
    buttons::UiEvent e;
    while (buttons::uiQueue() && xQueueReceive(buttons::uiQueue(), &e, 0) == pdTRUE) handleUi(e);

    uint32_t now = millis();
    uint32_t dtMs = now - lastTick;
    float dtH = dtMs / 3600000.0f;
    lastTick = now;
    // Пробег/энергия поездки (до интеграции с контроллером мотора скорость = 0).
    vstate::modify([&](VehicleState& s) {
      s.tripKm += s.speedKmh * dtH;
      if (s.battery.current_a > 0) s.tripWh += s.battery.power_w * dtH;
      static uint32_t movingMs = 0;                       // время в движении (для средней скорости)
      if (s.speedKmh > 0.5f) movingMs += dtMs;
      if (s.tripKm == 0) movingMs = 0;                    // после сброса поездки
      s.tripSeconds = movingMs / 1000;
    });
    VehicleState s = vstate::snapshot();
    ui::autoSwitch(g_nav, s.battery.charging, s.speedKmh);
    g_hal->setBacklight(ui::backlightPct(s.ambientLux, CFG_BL_MIN_PCT, CFG_BL_MAX_PCT, CFG_BL_LUX_FULL));
    g_hal->render(g_nav.screen, s, now);
    vTaskDelayUntil(&last, pdMS_TO_TICKS(periodMs));
  }
}
}  // namespace display
