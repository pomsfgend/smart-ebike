#include "front_turn.h"
#include "led_bus.h"

namespace front_turn {
void init() {}

void render(const VehicleState& s, uint32_t now) {
  // Габарит: тусклый янтарный, когда горит основной свет (не путать с фарой!).
  CRGB idle = s.mainLightOn ? CRGB(40, 18, 0) : CRGB::Black;
  fill_solid(leds::frontL, CFG_LED_FRONT_COUNT, idle);
  fill_solid(leds::frontR, CFG_LED_FRONT_COUNT, idle);
  if (s.turn == TurnState::None) return;

  bool on = vstate::blinkOn(s, now);
  float phase = (float)((now - s.turnSinceMs) % CFG_BLINK_PERIOD_MS) / CFG_BLINK_ON_MS;
  bool left  = s.turn == TurnState::Left  || s.turn == TurnState::Hazard;
  bool right = s.turn == TurnState::Right || s.turn == TurnState::Hazard;
  // Индекс 0 — ближе к рулю/центру; бегущий огонь идёт наружу.
  if (left)  { if (on) leds::sweep(leds::frontL, 0, CFG_LED_FRONT_COUNT, phase, leds::kAmber, false);
               else fill_solid(leds::frontL, CFG_LED_FRONT_COUNT, CRGB::Black); }
  if (right) { if (on) leds::sweep(leds::frontR, 0, CFG_LED_FRONT_COUNT, phase, leds::kAmber, false);
               else fill_solid(leds::frontR, CFG_LED_FRONT_COUNT, CRGB::Black); }
}
}  // namespace front_turn
