#include "underglow.h"
#include "led_bus.h"
#include "config_store.h"

namespace {
underglow::Mode g_mode = underglow::Mode::Follow;
constexpr uint16_t kLS = CFG_UNDERGLOW_LEFT_START,  kLN = CFG_UNDERGLOW_LEFT_COUNT;
constexpr uint16_t kRS = CFG_UNDERGLOW_RIGHT_START, kRN = CFG_UNDERGLOW_RIGHT_COUNT;
static_assert(kLS + kLN <= CFG_LED_UNDERGLOW_COUNT && kRS + kRN <= CFG_LED_UNDERGLOW_COUNT, "underglow layout");

void drawRoadMark(CRGB* b, uint16_t start, uint16_t n, uint32_t now, float speedKmh) {
  // Штрихи 4 вкл / 4 выкл, скорость бега пропорциональна скорости велосипеда (иллюзия неподвижной разметки).
  uint32_t shift = (uint32_t)(now * (0.002f + speedKmh * 0.0008f));
  for (uint16_t i = 0; i < n; ++i) b[start + i] = (((i + shift) / 4) % 2) ? CRGB(80, 80, 70) : CRGB::Black;
}
}  // namespace

namespace underglow {
void init() { g_mode = (Mode)config_store::get().underglowMode; }
void setMode(Mode m) { g_mode = m; }
Mode mode() { return g_mode; }

void render(const VehicleState& s, uint32_t now) {
  CRGB* b = leds::underglow;
  switch (g_mode) {
    case Mode::Off:
      fill_solid(b, CFG_LED_UNDERGLOW_COUNT, CRGB::Black);
      break;
    case Mode::Ambient: {
      uint8_t v = beatsin8(12, 30, 120);  // «дыхание»
      fill_solid(b, CFG_LED_UNDERGLOW_COUNT, CHSV(160, 200, v));
      break;
    }
    case Mode::Follow: {
      uint8_t v = s.mainLightOn ? (uint8_t)constrain(60 + s.speedKmh * 4, 60, 200) : 0;
      fill_solid(b, CFG_LED_UNDERGLOW_COUNT, CRGB(v, (uint8_t)(v * 0.8f), (uint8_t)(v * 0.5f)));
      break;
    }
    case Mode::RoadMark:
      fill_solid(b, CFG_LED_UNDERGLOW_COUNT, CRGB::Black);
      drawRoadMark(b, kLS, kLN, now, s.speedKmh);
      drawRoadMark(b, kRS, kRN, now, s.speedKmh);
      break;
  }

  if (s.turn != TurnState::None) {
    bool on = vstate::blinkOn(s, now);
    float phase = (float)((now - s.turnSinceMs) % CFG_BLINK_PERIOD_MS) / CFG_BLINK_ON_MS;
    bool left  = s.turn == TurnState::Left  || s.turn == TurnState::Hazard;
    bool right = s.turn == TurnState::Right || s.turn == TurnState::Hazard;
    // Волна идёт спереди назад (индекс 0 борта — у рулевой колонки).
    if (left)  { if (on) leds::sweep(b, kLS, kLN, phase, leds::kAmber, false); else fill_solid(b + kLS, kLN, CRGB::Black); }
    if (right) { if (on) leds::sweep(b, kRS, kRN, phase, leds::kAmber, false); else fill_solid(b + kRS, kRN, CRGB::Black); }
  }
  if (s.brake) {
    // Красный «хвост» — последние 8 светодиодов каждого борта.
    for (uint16_t i = 0; i < 8 && i < kLN; ++i) b[kLS + kLN - 1 - i] = leds::kRed;
    for (uint16_t i = 0; i < 8 && i < kRN; ++i) b[kRS + kRN - 1 - i] = leds::kRed;
  }
}
}  // namespace underglow
