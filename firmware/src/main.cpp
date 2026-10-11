// Smart E-Bike — точка входа. Создаёт задачи FreeRTOS; логика — в lib/<модуль>/.
// Раскладка задач и приоритетов: docs/core/FREERTOS.md
#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "pins.h"
#include "vehicle_state.h"
#include "config_store.h"
#include "inputs.h"
#include "comms.h"
#include "ota.h"
#include "led_bus.h"
#include "underglow.h"
#include "rear_light.h"
#include "front_turn.h"
#include "auto_light.h"
#include "battery_meter.h"
#include "buttons.h"
#include "display.h"
#include "i2c_bus.h"
#include "io_expander.h"
#include "ads1115.h"
#include "power_ctrl.h"
#include "grips.h"

namespace {
// Рендер всех лент: общий кадр → одна синхронная фаза мигания на всех лентах.
void ledRenderTask(void*) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    VehicleState s = vstate::snapshot();
    uint32_t now = millis();
    rear_light::render(s, now);      // безопасность в приоритете: стоп и повороты считаются первыми
    front_turn::render(s, now);
    underglow::render(s, now);
    leds::show();
    vTaskDelayUntil(&last, pdMS_TO_TICKS(1000 / CFG_LED_FPS));
  }
}

// Звук поворотника (тиканье): активный зуммер на PCA9555 P1.6, короткий импульс 15 мс на каждом фронте фазы.
void buzzerTask(void*) {
  bool prev = false;
  for (;;) {
    VehicleState s = vstate::snapshot();
    bool on = vstate::blinkOn(s, millis());
    if (on != prev) {
      prev = on;
      io_expander::write(pins::xio::BUZZER, true);
      vTaskDelay(pdMS_TO_TICKS(15));
      io_expander::write(pins::xio::BUZZER, false);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void telemetryTask(void*) {
  for (;;) {
    VehicleState s = vstate::snapshot();
    const BatteryData& b = s.battery;
    log_i("V=%.2f I=%.2f SoC=%.1f%% A:%s %.0f%% B:%s %.0f%% lux=%.0f light=%d brake=%d turn=%d",
          b.voltage_v, b.current_a, b.soc_pct,
          b.packs[0].online ? "on" : "off", b.packs[0].soc_pct,
          b.packs[1].online ? "on" : "off", b.packs[1].soc_pct,
          s.ambientLux, s.mainLightOn, s.brake, (int)s.turn);
    // TODO(BAT-07): BLE (Battery Service 0x180F + собственный сервис телеметрии).
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  vstate::init();
  config_store::load();
  i2c_bus::init();
  bool expOk = io_expander::init();   // первым: выходы предзаряда/клаксона/подогрева → 0
  log_i("PCA9555 %s, ADS1115 %s", expOk ? "ok" : "NOT FOUND (kill = active, contactor locked)", ads1115::init() ? "ok" : "NOT FOUND");

  inputs::init();
  leds::init();
  underglow::init();
  rear_light::init();
  front_turn::init();
  auto_light::init();
  battery_meter::init();
  power::init();
  grips::init();
  buttons::init();
  display::init();
  comms::init();
  ota::init();

  // Ядро 1 — реальное время (входы, свет); ядро 0 — Wi‑Fi/BLE, шины, батарея.
  //                       функция               имя         стек  арг  приор ядро
  xTaskCreatePinnedToCore(inputs::task,          "inputs",   3072, nullptr, 6, nullptr, 1);
  xTaskCreatePinnedToCore(buttons::task,         "buttons",  4096, nullptr, 5, nullptr, 1);
  xTaskCreatePinnedToCore(ledRenderTask,         "leds",     4096, nullptr, 5, nullptr, 1);
  xTaskCreatePinnedToCore(auto_light::task,      "autolight",3072, nullptr, 4, nullptr, 1);
  xTaskCreatePinnedToCore(buzzerTask,            "buzzer",   2048, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(power::task,           "power",    4096, nullptr, 5, nullptr, 0);
  xTaskCreatePinnedToCore(battery_meter::task,   "battery",  6144, nullptr, 4, nullptr, 0);
  xTaskCreatePinnedToCore(grips::task,           "grips",    3072, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(display::task,         "display",  8192, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(comms::task,           "comms",    4096, nullptr, 3, nullptr, 0);
  xTaskCreatePinnedToCore(ota::task,             "ota",      4096, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(telemetryTask,         "telemetry",4096, nullptr, 1, nullptr, 0);
}

void loop() { vTaskDelete(nullptr); }  // вся работа — в задачах
