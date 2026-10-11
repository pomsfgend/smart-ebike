#include "ina228.h"

namespace {
enum Reg : uint8_t { CONFIG = 0x00, ADC_CONFIG = 0x01, SHUNT_CAL = 0x02, VSHUNT = 0x04, VBUS = 0x05,
                     DIETEMP = 0x06, CURRENT = 0x07, POWER = 0x08, ENERGY = 0x09, CHARGE = 0x0A,
                     MANUFACTURER_ID = 0x3E };
int32_t signExtend(uint64_t v, uint8_t bits) {
  uint64_t m = 1ULL << (bits - 1);
  return (int32_t)((v ^ m) - m);
}
int64_t signExtend64(uint64_t v, uint8_t bits) {
  uint64_t m = 1ULL << (bits - 1);
  return (int64_t)((v ^ m) - m);
}
}  // namespace

bool INA228::begin(TwoWire& w, uint8_t addr, float shuntOhm, float maxCurrentA, uint8_t adcRange) {
  w_ = &w; addr_ = addr;
  ok_ = readN(MANUFACTURER_ID, 2) == 0x5449;  // "TI"
  if (!ok_) return false;
  currentLsb_ = maxCurrentA / 524288.0f;      // 2^19
  float cal = 13107.2e6f * currentLsb_ * shuntOhm;
  if (adcRange) cal *= 4;                      // ADCRANGE=1 (±40.96 мВ)
  config_ = adcRange ? (1u << 4) : 0;
  write16(CONFIG, config_);
  // Непрерывный режим шина+шунт+темп, время преобразования 1052 мкс, усреднение 64 → ~0.13 с на полный цикл.
  write16(ADC_CONFIG, (0xF << 12) | (5 << 9) | (5 << 6) | (5 << 3) | 3);
  write16(SHUNT_CAL, (uint16_t)cal);
  resetAccumulators();
  return true;
}

float INA228::busVoltage() { return (readN(VBUS, 3) >> 4) * 195.3125e-6f; }
float INA228::current()    { return signExtend(readN(CURRENT, 3) >> 4, 20) * currentLsb_; }
float INA228::power()      { return readN(POWER, 3) * 3.2f * currentLsb_; }
float INA228::energyJ()    { return (float)readN(ENERGY, 5) * 16.0f * 3.2f * currentLsb_; }
float INA228::chargeC()    { return (float)signExtend64(readN(CHARGE, 5), 40) * currentLsb_; }
float INA228::dieTemp()    { return (int16_t)readN(DIETEMP, 2) * 7.8125e-3f; }
// RSTACC (бит 14) самосбрасывается; ADCRANGE (бит 4) сохраняем.
void  INA228::resetAccumulators() { write16(CONFIG, (1u << 14) | config_); }

bool INA228::write16(uint8_t reg, uint16_t v) {
  w_->beginTransmission(addr_);
  w_->write(reg); w_->write(v >> 8); w_->write(v & 0xFF);
  return w_->endTransmission() == 0;
}

uint64_t INA228::readN(uint8_t reg, uint8_t bytes) {
  w_->beginTransmission(addr_);
  w_->write(reg);
  if (w_->endTransmission(false) != 0) return 0;
  if (w_->requestFrom(addr_, bytes) != bytes) return 0;
  uint64_t v = 0;
  for (uint8_t i = 0; i < bytes; ++i) v = (v << 8) | (uint8_t)w_->read();
  return v;
}
