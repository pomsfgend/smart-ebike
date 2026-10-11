#pragma once
// Общая шина I2C (INA228, BH1750, PCA9555, ADS1115, OLED) используется задачами на обоих ядрах.
// Любая составная транзакция (запись адреса регистра + чтение) — под этим замком:
//   { I2cLock l; Wire.beginTransmission(...); ... Wire.requestFrom(...); }
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace i2c_bus {
void init();
SemaphoreHandle_t mutex();
}

struct I2cLock {
  I2cLock()  { xSemaphoreTakeRecursive(i2c_bus::mutex(), portMAX_DELAY); }
  ~I2cLock() { xSemaphoreGiveRecursive(i2c_bus::mutex()); }
};
