#pragma once
// Минимальный драйвер TI INA228 (20 бит, шина до 85 В — подходит для 65.7 В; INA226 НЕ подходит: макс. 36 В).
// Использует аппаратные накопители ENERGY (J) и CHARGE (Кл) — интегрирование без потерь отсчётов.
#include <Arduino.h>
#include <Wire.h>

class INA228 {
 public:
  bool begin(TwoWire& w, uint8_t addr, float shuntOhm, float maxCurrentA, uint8_t adcRange);
  float busVoltage();   // В
  float current();      // А (знак: >0 — ток через шунт от IN+ к IN−; разводка — разряд)
  float power();        // Вт
  float energyJ();      // Дж с момента resetAccumulators()
  float chargeC();      // Кл со знаком
  float dieTemp();      // °C
  void  resetAccumulators();
  bool  ok() const { return ok_; }

 private:
  bool     write16(uint8_t reg, uint16_t v);
  uint64_t readN(uint8_t reg, uint8_t bytes);
  TwoWire* w_ = nullptr;
  uint8_t  addr_ = 0x40;
  float    currentLsb_ = 0;
  uint16_t config_ = 0;
  bool     ok_ = false;
};
