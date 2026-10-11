#include "io_expander.h"
#include "config.h"
#include "i2c_bus.h"
#include "pins.h"
#include <Wire.h>

namespace {
bool g_ok = false;
uint8_t g_out1 = 0x00;   // теневой регистр выходов порта 1 (всё выключено)

bool wr(uint8_t reg, uint8_t v) {
  I2cLock l;
  Wire.beginTransmission(CFG_PCA9555_ADDR); Wire.write(reg); Wire.write(v);
  return Wire.endTransmission() == 0;
}
}  // namespace

namespace io_expander {
bool init() {
  // Сначала выходы = 0 (предзаряд, зуммер, клаксон, подогрев выключены), потом направление.
  g_ok = wr(0x03, g_out1) && wr(0x06, 0xFF) && wr(0x07, pins::xio::P1_DIR_MASK);
  return g_ok;
}
bool ok() { return g_ok; }

bool readAll(uint16_t& in) {
  if (!g_ok) return false;
  I2cLock l;
  Wire.beginTransmission(CFG_PCA9555_ADDR); Wire.write(0x00);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)CFG_PCA9555_ADDR, (uint8_t)2) != 2) return false;
  in = Wire.read();
  in |= (uint16_t)Wire.read() << 8;
  return true;
}

bool write(uint8_t line, bool level) {
  if (!g_ok || line < 8) return false;
  I2cLock l;
  uint8_t bit = 1u << (line - 8);
  uint8_t next = level ? (g_out1 | bit) : (g_out1 & ~bit);
  if (next == g_out1) return true;
  g_out1 = next;
  return wr(0x03, g_out1);
}

bool input(uint8_t line, bool& level) {
  uint16_t in;
  if (!readAll(in)) return false;
  level = in & (1u << line);
  return true;
}
}  // namespace io_expander
