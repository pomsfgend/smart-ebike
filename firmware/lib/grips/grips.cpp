#include "grips.h"
#include "grips_logic.h"
#include "ads1115.h"
#include "config.h"
#include "io_expander.h"
#include "pins.h"
#include "power_ctrl.h"
#include "vehicle_state.h"
#include <Arduino.h>

namespace {
constexpr uint8_t kCh[2]  = {4, 5};   // каналы LEDC (таймер 2: ch4/ch5 — общая частота 200 Гц)
constexpr uint8_t kPin[2] = {pins::GRIP_PWM_L, pins::GRIP_PWM_R};
constexpr uint8_t kNtc[2] = {pins::adc::GRIP_NTC_L, pins::adc::GRIP_NTC_R};
const grips::Params kP{CFG_GRIP_LEVELS, CFG_GRIP_T_LEVEL1_C, CFG_GRIP_T_STEP_C, CFG_GRIP_T_MAX_C, CFG_GRIP_MAX_A_EACH,
                       CFG_GRIP_BUDGET_A, (uint32_t)CFG_GRIP_AUTO_OFF_MIN * 60000u, CFG_GRIP_LOW_SOC_PCT};
grips::Controller g_ctl[2] = {grips::Controller(kP), grips::Controller(kP)};
volatile uint8_t g_req = 0xFF;   // запрос уровня от других задач (0xFF — нет)
float g_temp[2] = {-999, -999};
}  // namespace

namespace grips {
void init() {
  for (int i = 0; i < 2; ++i) { ledcSetup(kCh[i], CFG_GRIP_PWM_HZ, 10); ledcAttachPin(kPin[i], kCh[i]); ledcWrite(kCh[i], 0); }
  io_expander::write(pins::xio::GRIPS_PWR_EN, false);
}
void setLevel(uint8_t l) { g_req = l; }
void cycleLevel() { setLevel((level() + 1) % (CFG_GRIP_LEVELS + 1)); }
uint8_t level() { return g_ctl[0].level(); }
float tempC(uint8_t i) { return i < 2 ? g_temp[i] : -999; }

void task(void*) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    uint32_t now = millis();
    if (g_req != 0xFF) { for (auto& c : g_ctl) c.setLevel(g_req, now); g_req = 0xFF; }
    VehicleState s = vstate::snapshot();
    bool rail = power::loadOn();
    uint8_t active = (g_ctl[0].level() ? 1 : 0) + (g_ctl[1].level() ? 1 : 0);
    float cap = budgetDutyCap(CFG_GRIP_BUDGET_A, CFG_GRIP_MAX_A_EACH, active);
    bool any = false;
    for (int i = 0; i < 2; ++i) {
      float v = ads1115::readVolts(kNtc[i]);
      g_temp[i] = v < 0 ? -999 : ntcToC(v);
      float d = g_ctl[i].update(g_temp[i], s.battery.valid ? s.battery.soc_pct : 100, rail, cap, now, 0.2f);
      ledcWrite(kCh[i], (uint32_t)(d * 1023));
      any |= d > 0;
    }
    io_expander::write(pins::xio::GRIPS_PWR_EN, any);   // резервный ключ: снят — подогрев обесточен
    vTaskDelayUntil(&last, pdMS_TO_TICKS(200));
  }
}
}  // namespace grips
