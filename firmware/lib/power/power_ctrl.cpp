#include "power_ctrl.h"
#include "ads1115.h"
#include "config.h"
#include "io_expander.h"
#include "pins.h"
#include "vehicle_state.h"
#include "led_bus.h"
#include <Arduino.h>
#include <esp_sleep.h>

namespace {
constexpr uint8_t kCoilCh = 2;            // канал LEDC
constexpr float   kVLoadDivider = 31.0f;  // 300 кОм + 10 кОм → ×31 (65.7 В → 2.12 В); калибровка — POWER-02
volatile bool g_request = false;
volatile bool g_loadOn = false;
power::Logic g_logic({CFG_PRECHARGE_MIN_MS, CFG_PRECHARGE_TIMEOUT_MS, CFG_CONTACTOR_PULLIN_MS, CFG_AUX_TIMEOUT_MS,
                      CFG_PRECHARGE_RATIO, CFG_COIL_NOMINAL_V, CFG_CONTACTOR_HOLD_PCT / 100.0f, CFG_CONTACTOR_OPEN_MAX_A});
uint32_t g_offSince = 0;

void enterDeepSleep() {
  // Пробуждение: сжатие ручки тормоза (GPIO14 → LOW). Перед сном — всё выключено.
  log_w("deep sleep");
  leds::setPowerEnabled(false);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)pins::BRAKE_L, 0);
  esp_deep_sleep_start();
}
}  // namespace

namespace power {
void init() {
  ledcSetup(kCoilCh, CFG_CONTACTOR_PWM_HZ, 10);
  ledcAttachPin(pins::CONTACTOR_PWM, kCoilCh);
  ledcWrite(kCoilCh, 0);
  io_expander::write(pins::xio::PRECHARGE_RELAY, false);
  g_offSince = millis();
}

void requestOn(bool on) { g_request = on; }
bool loadOn() { return g_loadOn; }
State state() { return g_logic.state(); }
Fault fault() { return g_logic.fault(); }

void task(void*) {
  TickType_t last = xTaskGetTickCount();
  uint8_t slow = 0; float vLoad = 0;
  for (;;) {
    VehicleState s = vstate::snapshot();
    Inputs in{};
    in.request = g_request;
    bool lvl = true;
    in.kill = io_expander::input(pins::xio::KILL_SENSE, lvl) ? !lvl : true;     // нет связи с расширителем → считаем kill
    in.hasAux = CFG_CONTACTOR_HAS_AUX && io_expander::input(pins::xio::CONTACTOR_AUX, lvl);
    in.auxClosed = in.hasAux && !lvl;
    in.bmsFault = false;
    for (uint8_t i = 0; i < CFG_BATT_PACKS_P; ++i)
      if (s.battery.packs[i].online && !s.battery.packs[i].dsgFet) in.bmsFault = true;
    if (++slow >= 5) { slow = 0; float v = ads1115::readVolts(pins::adc::V_LOAD); if (v >= 0) vLoad = v * kVLoadDivider; }
    in.vLoad = vLoad;
    in.vPack = s.battery.voltage_v;
    in.currentA = s.battery.current_a;

    Outputs o = g_logic.update(in, millis());
    io_expander::write(pins::xio::PRECHARGE_RELAY, o.precharge);
    ledcWrite(kCoilCh, (uint32_t)(o.coilDuty * 1023));
    g_loadOn = o.loadOn;

    if (g_logic.state() == State::Off && !g_request) {
      if (millis() - g_offSince > CFG_SLEEP_AFTER_OFF_MS && s.speedKmh < 0.5f && !s.battery.charging) enterDeepSleep();
    } else g_offSince = millis();
    vTaskDelayUntil(&last, pdMS_TO_TICKS(10));
  }
}
}  // namespace power
