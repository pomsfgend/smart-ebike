#pragma once
// Чистая логика кнопок руля (без Arduino, тестируется на ПК):
//  • антидребезг по времени;
//  • события: Press, Release, Short (отпущена до long), Long (однократно при удержании ≥ long), Repeat (автоповтор);
//  • таблица назначений: физическая кнопка → действие на короткое/длинное нажатие, тип (кнопка / фиксируемый рычаг).
#include <stdint.h>

namespace buttons {

enum class Event : uint8_t { None, Press, Release, Short, Long, Repeat };

enum class Action : uint8_t {
  None,
  TurnLeft, TurnRight,     // для кнопок — переключение (toggle), для фиксируемого рычага — по уровню
  Hazard,                  // toggle
  LightCycle,              // OFF → AUTO → ON → OFF (если нет 3-позиционного переключателя)
  LightAuto, LightOn,      // контакты 3-позиционного переключателя (по уровню)
  HighBeam,                // дальний (по уровню/toggle) — задел
  Horn,                    // по уровню, пока нажата
  MenuUp, MenuDown, MenuSelect, MenuBack,
  TripReset,
  UnderglowMode,           // смена режима днища
  PowerToggle,             // «зажигание»: главный контактор вкл/выкл (длинное нажатие)
  GripCycle,               // подогрев ручек 0→1→…→5→0
};

enum class Kind : uint8_t { Momentary, Level };   // Level — фиксируемый рычаг/тумблер: важен уровень, а не нажатия

struct Mapping {
  Kind   kind;
  Action shortAction;      // для Level — действие по уровню
  Action longAction;       // None — нет; для навигации меню удобно Repeat = shortAction
  bool   repeat;           // автоповтор shortAction при удержании
};

struct Timing { uint32_t debounceMs, longMs, repeatMs; };

class Decoder {
 public:
  // raw — «нажата» (после инверсии активного уровня). Возвращает одно событие за вызов.
  Event update(bool raw, uint32_t now, const Timing& t, bool repeat) {
    if (raw != lastRaw_) { lastRaw_ = raw; changedAt_ = now; }
    if (raw != stable_ && now - changedAt_ >= t.debounceMs) {
      stable_ = raw;
      if (stable_) { pressedAt_ = now; longFired_ = false; lastRepeat_ = now; return Event::Press; }
      bool wasLong = longFired_;
      longFired_ = false;
      return wasLong ? Event::Release : Event::Short;   // Short сопровождается отпусканием
    }
    if (stable_) {
      if (!repeat && !longFired_ && now - pressedAt_ >= t.longMs) { longFired_ = true; return Event::Long; }
      if (repeat && now - pressedAt_ >= t.longMs && now - lastRepeat_ >= t.repeatMs) {
        lastRepeat_ = now; longFired_ = true; return Event::Repeat;
      }
    }
    return Event::None;
  }
  bool pressed() const { return stable_; }

 private:
  bool lastRaw_ = false, stable_ = false, longFired_ = false;
  uint32_t changedAt_ = 0, pressedAt_ = 0, lastRepeat_ = 0;
};

// ADC-лестница: по напряжению (мВ) определить индекс кнопки, -1 — ничего не нажато.
// thresholds_mv — середины между соседними уровнями, по возрастанию; idle_mv — уровень «отпущено».
inline int ladderDecode(uint16_t mv, const uint16_t* levels_mv, uint8_t n, uint16_t tol_mv) {
  for (uint8_t i = 0; i < n; ++i) {
    int d = (int)mv - (int)levels_mv[i];
    if (d < 0) d = -d;
    if (d <= tol_mv) return i;
  }
  return -1;
}
}  // namespace buttons
