#pragma once
// Общий слой адресных лент: буферы, регистрация в FastLED, общая отрисовка и цвета.
// Все ленты обновляются одним FastLED.show() из задачи рендера (≈60 кадр/с),
// поэтому мигание спереди, сзади и на днище синхронно по построению.
#include <FastLED.h>
#include "config.h"

namespace leds {
extern CRGB underglow[CFG_LED_UNDERGLOW_COUNT];
extern CRGB rear[CFG_LED_REAR_COUNT];
extern CRGB frontL[CFG_LED_FRONT_COUNT];
extern CRGB frontR[CFG_LED_FRONT_COUNT];

// Цвета сигналов (подобраны под SK6812/WS2812B; уточнить на реальной ленте — задача LED-02).
constexpr CRGB kAmber   = CRGB(255, 110, 0);   // поворотник (жёлто-оранжевый)
constexpr CRGB kRed     = CRGB(255, 0, 0);
constexpr CRGB kTailRed = CRGB(60, 0, 0);      // габарит ≈ 1/4 яркости стопа
constexpr CRGB kWhite   = CRGB(255, 255, 255);

void init();
void setPowerEnabled(bool on);   // DC-DC 5 В для лент
void show();

// Последовательная «бегущая» заливка (sequential turn). progress 0..1.
void sweep(CRGB* buf, uint16_t start, uint16_t count, float progress, CRGB color, bool reverse);
}  // namespace leds
