#pragma once
// Конечный автомат главного контактора с предзарядом (чистый C++, тестируется на ПК).
//
//  Off ──request──► Precharge ──V_load ≥ 92 % V_pack (≥200 мс)──► PullIn ──150 мс──► Hold(On)
//   ▲                  │ таймаут 3 с / kill / BMS fault                │ aux не замкнулся
//   │                  ▼                                               ▼
//   └──── Opening ◄── Fault (нужен сброс: выключить и включить заново) ◄┘
//  Hold ──request off (ток < 10 А) / kill / BMS fault──► Opening ──aux разомкнут──► Off
//
// Kill-выключатель размыкает цепь катушки АППАРАТНО; логика лишь фиксирует событие и снимает ШИМ.
#include <stdint.h>

namespace power {

enum class State : uint8_t { Off, Precharge, PullIn, On, Opening, Fault };
enum class Fault : uint8_t { None, PrechargeTimeout, AuxNotClosed, AuxStuckClosed, Kill, BmsFault, VoltageSense };

struct Inputs {
  bool  request;        // пользователь хочет «зажигание» (кнопка/дисплей)
  bool  kill;           // аварийный выключатель нажат (цепь катушки разомкнута)
  bool  bmsFault;       // любая BMS разомкнула MOSFET / критическая защита
  bool  auxClosed;      // вспомогательный контакт контактора
  bool  hasAux;         // aux подключён (иначе проверка по напряжению)
  float vPack, vLoad;   // В
  float currentA;       // ток пакета (>0 — разряд)
};

struct Outputs {
  bool    precharge = false;
  float   coilDuty  = 0;      // 0..1 скважность ШИМ катушки
  bool    loadOn    = false;  // для остальных модулей: шина 12 В и контроллер мотора под напряжением
};

struct Params {
  uint32_t prechargeMinMs, prechargeTimeoutMs, pullInMs, auxTimeoutMs;
  float    prechargeRatio, coilNominalV, holdFraction, openMaxA;
};

class Logic {
 public:
  explicit Logic(const Params& p) : p_(p) {}
  State state() const { return st_; }
  Fault fault() const { return fault_; }

  Outputs update(const Inputs& in, uint32_t now) {
    if (st_ != State::Off && st_ != State::Fault && st_ != State::Opening) {
      if (in.kill)     return toFault(Fault::Kill, now);
      if (in.bmsFault) return toFault(Fault::BmsFault, now);
    }
    switch (st_) {
      case State::Off:
        if (in.request && !in.kill && !in.bmsFault) {
          if (in.hasAux && in.auxClosed) return toFault(Fault::AuxStuckClosed, now);  // сварились контакты
          if (in.vPack < 10) return toFault(Fault::VoltageSense, now);
          go(State::Precharge, now);
        }
        break;
      case State::Precharge: {
        uint32_t t = now - since_;
        if (t >= p_.prechargeMinMs && in.vLoad >= p_.prechargeRatio * in.vPack) go(State::PullIn, now);
        else if (t >= p_.prechargeTimeoutMs) return toFault(Fault::PrechargeTimeout, now);
        break;
      }
      case State::PullIn:
        if (now - since_ >= p_.pullInMs) {
          if (in.hasAux && !in.auxClosed && now - since_ >= p_.pullInMs + p_.auxTimeoutMs) return toFault(Fault::AuxNotClosed, now);
          if (!in.hasAux || in.auxClosed) go(State::On, now);
        }
        break;
      case State::On:
        if (!in.request && in.currentA < p_.openMaxA) go(State::Opening, now);
        break;
      case State::Opening:
        if (!in.hasAux || !in.auxClosed) go(State::Off, now);
        else if (now - since_ >= p_.auxTimeoutMs) return toFault(Fault::AuxStuckClosed, now);
        break;
      case State::Fault:
        if (!in.request && !in.kill) { fault_ = Fault::None; go(State::Off, now); }  // сброс: снять запрос
        break;
    }
    return outputs(in);
  }

 private:
  void go(State s, uint32_t now) { st_ = s; since_ = now; }
  Outputs toFault(Fault f, uint32_t now) { fault_ = f; go(State::Fault, now); return Outputs{}; }
  Outputs outputs(const Inputs& in) const {
    Outputs o;
    float pull = in.vPack > 1 ? p_.coilNominalV / in.vPack : 0;   // компенсация по напряжению пакета
    if (pull > 1) pull = 1;
    switch (st_) {
      case State::Precharge: o.precharge = true; break;
      case State::PullIn:    o.precharge = true; o.coilDuty = pull; o.loadOn = true; break;
      case State::On:        o.coilDuty = pull * p_.holdFraction; o.loadOn = true; break;
      default: break;
    }
    return o;
  }
  Params   p_;
  State    st_ = State::Off;
  Fault    fault_ = Fault::None;
  uint32_t since_ = 0;
};
}  // namespace power
