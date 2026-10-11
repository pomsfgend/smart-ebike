// Бэкенд-заглушка: текстовое представление экранов в консоль. Используется, пока нет USE_LVGL.
#include "display_hal.h"
#include "config.h"
#include <Arduino.h>

#ifndef USE_LVGL
namespace {
const char* kScreenNames[] = {"MAIN", "BATTERY", "PACKS", "LIGHTS", "TRIP", "CHARGING"};
const char* kTurn[] = {"-", "<", ">", "<!>"};
const char* kLight[] = {"OFF", "AUTO", "ON"};

class LogDisplay : public DisplayHal {
 public:
  bool begin() override { return true; }
  void setBacklight(uint8_t pct) override { bl_ = pct; }
  const char* name() const override { return "log"; }
  void render(ui::Screen sc, const VehicleState& s, uint32_t now) override {
    if (now - last_ < 1000) return;
    last_ = now;
    const BatteryData& b = s.battery;
    switch (sc) {
      case ui::Screen::Main:
        log_i("[%s] %.1f km/h  SoC %.0f%%  turn %s  light %s%s  BL %u%%", kScreenNames[(int)sc], s.speedKmh, b.soc_pct,
              kTurn[(int)s.turn], kLight[(int)s.lightSwitch], s.mainLightOn ? "*" : "", bl_);
        break;
      case ui::Screen::Packs:
        for (uint8_t i = 0; i < kMaxPacks; ++i) {
          const PackData& p = b.packs[i];
          log_i("[PACKS] %c %s SoC %.0f%% %.2fV %.1fA cells %u..%u mV", 'A' + i, p.online ? "on" : "OFF", p.soc_pct,
                p.voltage_v, p.current_a, p.cellMin_mv, p.cellMax_mv);
        }
        break;
      default:
        log_i("[%s] V %.2f I %.2f P %.0f SoC %.1f%% trip %.1f km %.0f Wh", kScreenNames[(int)sc], b.voltage_v,
              b.current_a, b.power_w, b.soc_pct, s.tripKm, s.tripWh);
    }
  }
 private:
  uint32_t last_ = 0;
  uint8_t bl_ = 100;
};
}  // namespace

DisplayHal& displayBackend() { static LogDisplay d; return d; }
#endif
