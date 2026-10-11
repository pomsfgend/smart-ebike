#include "buttons.h"
#include "config.h"
#include "pins.h"
#include "vehicle_state.h"
#include "underglow.h"
#include "io_expander.h"
#include "power_ctrl.h"
#include "grips.h"
#include <Arduino.h>
#include <Wire.h>

namespace {
using namespace buttons;

// Назначение по умолчанию для «типового» пульта (5–7 кнопок). ПЕРЕНАЗНАЧИТЬ после уточнения модели (BTN-01).
//  idx  физическая кнопка        тип        короткое              длинное              повтор
const Mapping kDefaultMap[] = {
  /*0*/ {Kind::Momentary, Action::TurnLeft,   Action::None,          false},  // «←» поворот влево
  /*1*/ {Kind::Momentary, Action::TurnRight,  Action::None,          false},  // «→» поворот вправо
  /*2*/ {Kind::Momentary, Action::Hazard,     Action::None,          false},  // «⚠» аварийка
  /*3*/ {Kind::Momentary, Action::LightCycle, Action::HighBeam,      false},  // «☀» свет: OFF/AUTO/ON, долго — дальний
  /*4*/ {Kind::Momentary, Action::Horn,       Action::None,          false},  // «📯» клаксон (по уровню)
  /*5*/ {Kind::Momentary, Action::MenuSelect, Action::TripReset,     false},  // «M» меню / долго — сброс поездки
  /*6*/ {Kind::Momentary, Action::MenuUp,     Action::None,          true},   // «+» (если есть)
  /*7*/ {Kind::Momentary, Action::MenuDown,   Action::UnderglowMode, true},   // «−» (если есть)
  // P1.x заняты силовой частью; «зажигание» и подогрев по умолчанию — длинные нажатия:
  //   долгое «⚠» — нельзя (аварийка срабатывает по нажатию), поэтому PowerToggle/GripCycle назначаются
  //   в NVS после уточнения модели пульта (BTN-01). Временный вариант — экранное меню.
};
constexpr uint8_t kCount = sizeof(kDefaultMap) / sizeof(kDefaultMap[0]);
static_assert(kCount <= kMaxButtons, "too many buttons");

Decoder g_dec[kCount];
QueueHandle_t g_ui = nullptr;
const Timing kTiming{CFG_BTN_DEBOUNCE_MS, CFG_BTN_LONG_MS, CFG_BTN_REPEAT_MS};

// ---------- Бэкенды: заполняют raw[i] = «нажата» ----------
#if CFG_BUTTONS_BACKEND == BUTTONS_BACKEND_GPIO
const uint8_t kPins[] = {pins::BTN_GPIO_TURN_L, pins::BTN_GPIO_TURN_R, pins::BTN_GPIO_HAZARD,
                         pins::BTN_GPIO_LIGHT_A, pins::BTN_GPIO_LIGHT_B};
void backendInit() { for (uint8_t p : kPins) pinMode(p, INPUT_PULLUP); }
void backendRead(bool* raw) {
  for (uint8_t i = 0; i < kCount; ++i) raw[i] = i < sizeof(kPins) ? digitalRead(kPins[i]) == LOW : false;
}
#elif CFG_BUTTONS_BACKEND == BUTTONS_BACKEND_ADC
// Уровни лестницы (мВ) — подобрать под резисторы пульта (BTN-03). Одновременно нажатые кнопки не различаются.
const uint16_t kLevels[] = {300, 750, 1200, 1650, 2100, 2550};
void backendInit() { analogSetPinAttenuation(pins::BTN_ADC, ADC_11db); }
void backendRead(bool* raw) {
  int idx = ladderDecode((uint16_t)analogReadMilliVolts(pins::BTN_ADC), kLevels, sizeof(kLevels) / 2, 150);
  for (uint8_t i = 0; i < kCount; ++i) raw[i] = (i == idx);
}
#else  // PCA9555 (общий драйвер lib/core/io_expander): P0.0..P0.7 — кнопки, активный LOW
void backendInit() {}
void backendRead(bool* raw) {
  for (uint8_t i = 0; i < kCount; ++i) raw[i] = false;
  uint16_t in;
  if (!io_expander::readAll(in)) return;
  for (uint8_t i = 0; i < kCount && i < 8; ++i) raw[i] = !(in & (1u << i));
}
#endif

// Клаксон — всегда выход PCA9555 P1.7 (расширитель обязателен для силовой части).
void setHornOutput(bool on) { io_expander::write(pins::xio::HORN, on); }

void sendUi(UiEvent e) { if (g_ui) xQueueSend(g_ui, &e, 0); }

void toggleTurn(VehicleState& s, TurnState t, uint32_t now) {
  TurnState next = (s.turn == t) ? TurnState::None : t;
  if (s.turn == TurnState::Hazard) next = t;  // поворот при аварийке — переключает на поворот
  s.turn = next; s.turnSinceMs = now;
}

bool g_lightA = false, g_lightB = false;   // контакты 3-позиционного переключателя (Kind::Level)

// Фиксируемый рычаг/тумблер: состояние определяется уровнем контакта.
void applyLevel(Action a, bool on, uint32_t now) {
  vstate::modify([&](VehicleState& s) {
    switch (a) {
      case Action::TurnLeft:
      case Action::TurnRight: {
        TurnState t = (a == Action::TurnLeft) ? TurnState::Left : TurnState::Right;
        if (s.turn == TurnState::Hazard) break;                 // аварийка приоритетнее рычага
        TurnState next = on ? t : (s.turn == t ? TurnState::None : s.turn);
        if (next != s.turn) { s.turn = next; s.turnSinceMs = now; }
        break;
      }
      case Action::LightAuto: g_lightA = on; break;
      case Action::LightOn:   g_lightB = on; break;
      case Action::Horn:      s.horn = on; break;
      default: break;
    }
    if (a == Action::LightAuto || a == Action::LightOn)
      s.lightSwitch = g_lightB ? LightSwitch::On : g_lightA ? LightSwitch::Auto : LightSwitch::Off;
  });
}

void apply(Action a, Event ev, uint32_t now) {
  vstate::modify([&](VehicleState& s) {
    switch (a) {
      case Action::TurnLeft:  if (ev == Event::Press) toggleTurn(s, TurnState::Left, now); break;
      case Action::TurnRight: if (ev == Event::Press) toggleTurn(s, TurnState::Right, now); break;
      case Action::Hazard:
        if (ev == Event::Press) { s.turn = (s.turn == TurnState::Hazard) ? TurnState::None : TurnState::Hazard; s.turnSinceMs = now; }
        break;
      case Action::LightCycle:
        if (ev == Event::Short) s.lightSwitch = (LightSwitch)(((uint8_t)s.lightSwitch + 1) % 3);
        break;
      default: break;
    }
  });
  if (ev != Event::Short) return;   // UI-действия — по короткому нажатию / автоповтору / long
  switch (a) {
    case Action::MenuUp:     sendUi(UiEvent::Up); break;
    case Action::MenuDown:   sendUi(UiEvent::Down); break;
    case Action::MenuSelect: sendUi(UiEvent::Select); break;
    case Action::MenuBack:   sendUi(UiEvent::Back); break;
    case Action::TripReset:  sendUi(UiEvent::TripReset); break;
    case Action::UnderglowMode:
      underglow::setMode((underglow::Mode)(((uint8_t)underglow::mode() + 1) % 4)); break;
    case Action::HighBeam:   break;  // TODO(AL-04)
    case Action::PowerToggle: power::requestOn(power::state() == power::State::Off)  /* в Fault: первое нажатие — сброс */; break;
    case Action::GripCycle:  grips::cycleLevel(); break;
    default: break;
  }
}
}  // namespace

namespace buttons {
QueueHandle_t uiQueue() { return g_ui; }
const Mapping* mappingTable(uint8_t& count) { count = kCount; return kDefaultMap; }

void init() {
  g_ui = xQueueCreate(16, sizeof(UiEvent));
  backendInit();
}

void task(void*) {
  TickType_t last = xTaskGetTickCount();
  bool raw[kCount];
  for (;;) {
    uint32_t now = millis();
    backendRead(raw);
    for (uint8_t i = 0; i < kCount; ++i) {
      const Mapping& m = kDefaultMap[i];
      Event ev = g_dec[i].update(raw[i], now, kTiming, m.repeat);
      if (ev == Event::None) continue;
      bool pressed = g_dec[i].pressed();
      if (m.kind == Kind::Level || m.shortAction == Action::Horn) {
        if (ev == Event::Press || ev == Event::Short || ev == Event::Release) applyLevel(m.shortAction, pressed, now);
        continue;
      }
      if (ev == Event::Long && m.longAction != Action::None) apply(m.longAction, Event::Short, now);
      else if (ev == Event::Repeat) apply(m.shortAction, Event::Short, now);
      else if (ev == Event::Press || ev == Event::Short) apply(m.shortAction, ev, now);
    }
    // Автосброс поворота для кнопок без фиксации.
    if (CFG_TURN_AUTO_OFF_MS > 0) {
      vstate::modify([&](VehicleState& s) {
        if ((s.turn == TurnState::Left || s.turn == TurnState::Right) && now - s.turnSinceMs > CFG_TURN_AUTO_OFF_MS)
          s.turn = TurnState::None;
      });
    }
    setHornOutput(vstate::snapshot().horn);
    vTaskDelayUntil(&last, pdMS_TO_TICKS(5));
  }
}
}  // namespace buttons
