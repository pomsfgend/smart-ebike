#include "inputs.h"
#include "pins.h"
#include "vehicle_state.h"
#include <Arduino.h>

namespace {
constexpr uint32_t kPollMs = 5;
constexpr uint8_t  kStable = 4;   // 20 мс стабильного уровня

struct Debounced {
  uint8_t pin; bool state = false; uint8_t cnt = 0;
  explicit Debounced(uint8_t p) : pin(p) {}
  bool update() {
    bool raw = digitalRead(pin) == LOW;  // активный LOW
    if (raw != state) { if (++cnt >= kStable) { state = raw; cnt = 0; } } else cnt = 0;
    return state;
  }
};

Debounced brakeL{pins::BRAKE_L}, brakeR{pins::BRAKE_R};
}  // namespace

namespace inputs {
void init() {
  pinMode(pins::BRAKE_L, INPUT_PULLUP);
  pinMode(pins::BRAKE_R, INPUT_PULLUP);
}

void task(void*) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    bool l = brakeL.update(), r = brakeR.update();
    bool brake = l || r;
    vstate::modify([&](VehicleState& s) { s.brake = brake; });
    vTaskDelayUntil(&last, pdMS_TO_TICKS(kPollMs));
  }
}
}  // namespace inputs
