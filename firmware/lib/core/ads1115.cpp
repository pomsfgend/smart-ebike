#include "ads1115.h"
#include "i2c_bus.h"
#include <Arduino.h>
#include <Wire.h>

namespace {
constexpr uint8_t kAddr = 0x48;
bool g_ok = false;
}

namespace ads1115 {
bool init() {
  I2cLock l;
  Wire.beginTransmission(kAddr);
  g_ok = Wire.endTransmission() == 0;
  return g_ok;
}
bool ok() { return g_ok; }

float readVolts(uint8_t ch) {
  if (!g_ok || ch > 3) return -1;
  // OS=1, MUX=1xx (AINx–GND), PGA=001 (±4.096 В), MODE=1 (single), DR=101 (250 SPS), компаратор выкл.
  uint16_t cfg = 0x8000 | ((0x4 | ch) << 12) | (0x1 << 9) | (1 << 8) | (0x5 << 5) | 0x3;
  I2cLock l;
  Wire.beginTransmission(kAddr);
  Wire.write(0x01); Wire.write(cfg >> 8); Wire.write(cfg & 0xFF);
  if (Wire.endTransmission() != 0) return -1;
  delay(5);   // 250 SPS → 4 мс; задача держит замок I2C ≈5 мс — допустимо при периодах ≥100 мс
  Wire.beginTransmission(kAddr); Wire.write(0x00);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(kAddr, (uint8_t)2) != 2) return -1;
  int16_t raw = (int16_t)((Wire.read() << 8) | Wire.read());
  return raw * 4.096f / 32768.0f;
}
}  // namespace ads1115
