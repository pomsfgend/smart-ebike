#include "i2c_bus.h"
#include "pins.h"
#include <Wire.h>

namespace {
SemaphoreHandle_t g_m = nullptr;
}

namespace i2c_bus {
void init() {
  if (!g_m) g_m = xSemaphoreCreateRecursiveMutex();
  Wire.begin(pins::I2C_SDA, pins::I2C_SCL, 400000);
  Wire.setTimeOut(20);
}
SemaphoreHandle_t mutex() { return g_m; }
}
