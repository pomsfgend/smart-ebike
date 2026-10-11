#pragma once
// Чистая логика автосвета без зависимостей от Arduino — тестируется на ПК (firmware/test/).
// Положения переключателя: OFF — всегда выкл, ON — всегда вкл, AUTO — по освещённости с гистерезисом
// по порогу (luxOn < luxOff) и по времени (быстро включаем, медленно выключаем).
#include <stdint.h>

namespace auto_light {

enum class Switch : uint8_t { Off = 0, Auto = 1, On = 2 };

struct Params {
  float    luxOn;          // ниже — кандидат на включение
  float    luxOff;         // выше — кандидат на выключение
  uint32_t onDelayMs;
  uint32_t offDelayMs;
};

class Logic {
 public:
  explicit Logic(const Params& p) : p_(p) {}
  // lux < 0 — датчик неисправен: в AUTO свет включается (fail-safe).
  bool update(Switch sw, float lux, uint32_t nowMs) {
    if (sw == Switch::Off) { light_ = false; pending_ = false; return light_; }
    if (sw == Switch::On)  { light_ = true;  pending_ = false; return light_; }
    if (lux < 0) { light_ = true; pending_ = false; return light_; }

    bool wantChange = light_ ? (lux > p_.luxOff) : (lux < p_.luxOn);
    if (!wantChange) { pending_ = false; return light_; }
    if (!pending_) { pending_ = true; since_ = nowMs; }
    uint32_t need = light_ ? p_.offDelayMs : p_.onDelayMs;
    if (nowMs - since_ >= need) { light_ = !light_; pending_ = false; }
    return light_;
  }
  bool light() const { return light_; }

 private:
  Params   p_;
  bool     light_   = false;
  bool     pending_ = false;
  uint32_t since_   = 0;
};
}  // namespace auto_light
