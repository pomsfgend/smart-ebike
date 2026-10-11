#include "auto_light.h"
#include "config.h"
#include "config_store.h"
#include "i2c_bus.h"
#include "pins.h"
#include "vehicle_state.h"
#include <Arduino.h>
#include <Wire.h>

namespace {
constexpr uint8_t kBh1750Addr = 0x23;
constexpr uint8_t kHeadlightCh = 0;     // канал LEDC
bool g_sensorOk = false;

bool bh1750Begin() {
  I2cLock l;
  Wire.beginTransmission(kBh1750Addr);
  Wire.write(0x01);                      // Power On
  if (Wire.endTransmission() != 0) return false;
  Wire.beginTransmission(kBh1750Addr);
  Wire.write(0x10);                      // Continuously H-Resolution Mode, 1 лк, 120 мс
  return Wire.endTransmission() == 0;
}

float bh1750Read() {
  I2cLock l;
  if (Wire.requestFrom(kBh1750Addr, (uint8_t)2) != 2) return -1;
  uint16_t raw = (uint16_t)Wire.read() << 8;
  raw |= Wire.read();
  return raw / 1.2f;
}

// Простое экспоненциальное сглаживание, чтобы тени деревьев не дёргали логику.
float g_luxFilt = -1;
float filt(float lux) {
  if (lux < 0) return lux;
  g_luxFilt = (g_luxFilt < 0) ? lux : g_luxFilt + 0.2f * (lux - g_luxFilt);
  return g_luxFilt;
}
}  // namespace

namespace auto_light {
void init() {
  g_sensorOk = bh1750Begin();
  log_i("BH1750 %s", g_sensorOk ? "ok" : "not found (fail-safe: AUTO => light ON)");
  // TODO(AL-02): альтернативы VEML7700 и LDR (analogRead(pins::LDR_SENSE)) по конфигу.
  ledcSetup(kHeadlightCh, 1000, 10);     // 1 кГц, 10 бит
  ledcAttachPin(pins::HEADLIGHT_PWM, kHeadlightCh);
  ledcWrite(kHeadlightCh, 0);
}

void task(void*) {
  const RuntimeConfig& c = config_store::get();
  Logic logic({c.autoLuxOn, c.autoLuxOff, CFG_AUTO_ON_DELAY_MS, CFG_AUTO_OFF_DELAY_MS});
  for (;;) {
    float lux = g_sensorOk ? filt(bh1750Read()) : -1;
    VehicleState s = vstate::snapshot();
    bool on = logic.update((Switch)s.lightSwitch, lux, millis());
    ledcWrite(kHeadlightCh, on ? 1023 : 0);   // TODO(AL-04): плавное включение, дальний/ближний
    vstate::modify([&](VehicleState& st) { st.ambientLux = lux; st.mainLightOn = on; });
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
}  // namespace auto_light
