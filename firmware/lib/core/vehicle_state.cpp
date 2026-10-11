#include "vehicle_state.h"
#include "config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
VehicleState g_state;
SemaphoreHandle_t g_mutex = nullptr;
}

namespace vstate {
void init() { if (!g_mutex) g_mutex = xSemaphoreCreateMutex(); }
void lock() { xSemaphoreTake(g_mutex, portMAX_DELAY); }
void unlock() { xSemaphoreGive(g_mutex); }
VehicleState& raw() { return g_state; }

VehicleState snapshot() {
  lock();
  VehicleState copy = g_state;
  unlock();
  return copy;
}

bool blinkOn(const VehicleState& s, uint32_t nowMs) {
  if (s.turn == TurnState::None) return false;
  // Первая вспышка начинается сразу при включении поворота.
  return ((nowMs - s.turnSinceMs) % CFG_BLINK_PERIOD_MS) < CFG_BLINK_ON_MS;
}
}  // namespace vstate
