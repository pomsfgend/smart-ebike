#include "ota.h"
#include "config.h"
#include "vehicle_state.h"
#include <WiFi.h>
#include <ArduinoOTA.h>

namespace ota {
void init() {
#if CFG_OTA_ENABLED
  // TODO(core-06): SSID/пароль из NVS или режим AP «smart-ebike-XXXX» + веб-страница настройки.
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(CFG_HOSTNAME);
  ArduinoOTA.setHostname(CFG_HOSTNAME);
  ArduinoOTA.onStart([] { log_w("OTA start"); });
  ArduinoOTA.onError([](ota_error_t e) { log_e("OTA error %u", e); });
  ArduinoOTA.begin();
#endif
}

void task(void*) {
  for (;;) {
#if CFG_OTA_ENABLED
    VehicleState s = vstate::snapshot();
    bool parked = s.speedKmh < 0.5f && fabsf(s.battery.current_a) < 2.0f;
    if (parked && WiFi.status() == WL_CONNECTED) ArduinoOTA.handle();
#endif
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
}  // namespace ota
