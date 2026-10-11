#include "rear_light.h"
#include "led_bus.h"

namespace {
constexpr uint16_t kSeg    = CFG_REAR_TURN_SEG;
constexpr uint16_t kCenter = CFG_LED_REAR_COUNT - 2 * kSeg;
static_assert(CFG_LED_REAR_COUNT > 2 * CFG_REAR_TURN_SEG, "rear strip too short");
}

namespace rear_light {
void init() {}

void render(const VehicleState& s, uint32_t now) {
  CRGB* b = leds::rear;
  // База: стоп (полная яркость на всю ленту) или габарит (если включён основной свет).
  CRGB base = s.brake ? leds::kRed : (s.mainLightOn ? leds::kTailRed : CRGB::Black);
  fill_solid(b, CFG_LED_REAR_COUNT, base);

  if (s.turn == TurnState::None) return;
  bool on = vstate::blinkOn(s, now);
  float phase = (float)((now - s.turnSinceMs) % CFG_BLINK_PERIOD_MS) / CFG_BLINK_ON_MS;
  bool left  = s.turn == TurnState::Left  || s.turn == TurnState::Hazard;
  bool right = s.turn == TurnState::Right || s.turn == TurnState::Hazard;
  // В сегменте поворота стоп не рисуем — однозначность сигнала (жёлтый ≠ красный).
  if (left)  { if (on) leds::sweep(b, 0, kSeg, phase, leds::kAmber, true);
               else fill_solid(b, kSeg, CRGB::Black); }
  if (right) { if (on) leds::sweep(b, kSeg + kCenter, kSeg, phase, leds::kAmber, false);
               else fill_solid(b + kSeg + kCenter, kSeg, CRGB::Black); }
}
}  // namespace rear_light
