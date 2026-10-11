#include "led_bus.h"
#include "pins.h"

namespace leds {
CRGB underglow[CFG_LED_UNDERGLOW_COUNT];
CRGB rear[CFG_LED_REAR_COUNT];
CRGB frontL[CFG_LED_FRONT_COUNT];
CRGB frontR[CFG_LED_FRONT_COUNT];

void init() {
  pinMode(pins::LED_5V_EN, OUTPUT);
  setPowerEnabled(true);
  // ESP32-S3: 4 канала RMT TX — ровно 4 ленты. Для большего числа — I2S/LCD-драйвер (см. ARCHITECTURE.md).
  FastLED.addLeds<WS2812B, pins::LED_UNDERGLOW, GRB>(underglow, CFG_LED_UNDERGLOW_COUNT);
  FastLED.addLeds<WS2812B, pins::LED_REAR, GRB>(rear, CFG_LED_REAR_COUNT);
  FastLED.addLeds<WS2812B, pins::LED_FRONT_L, GRB>(frontL, CFG_LED_FRONT_COUNT);
  FastLED.addLeds<WS2812B, pins::LED_FRONT_R, GRB>(frontR, CFG_LED_FRONT_COUNT);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, CFG_LED_MAX_MA);
  FastLED.setDither(0);
  FastLED.clear(true);
}

void setPowerEnabled(bool on) { digitalWrite(pins::LED_5V_EN, on ? HIGH : LOW); }

void show() { FastLED.show(); }

void sweep(CRGB* buf, uint16_t start, uint16_t count, float progress, CRGB color, bool reverse) {
  if (progress < 0) progress = 0;
  if (progress > 1) progress = 1;
  uint16_t lit = (uint16_t)(progress * count + 0.5f);
  for (uint16_t i = 0; i < count; ++i) {
    uint16_t idx = reverse ? (start + count - 1 - i) : (start + i);
    buf[idx] = (i < lit) ? color : CRGB::Black;
  }
}
}  // namespace leds
