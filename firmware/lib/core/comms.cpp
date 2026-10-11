#include "comms.h"
#include "pins.h"
#include "vehicle_state.h"
#include <Arduino.h>
#include "driver/twai.h"

namespace {
bool g_canOk = false;
}

namespace comms {
void init() {
  Serial1.begin(9600, SERIAL_8N1, pins::MC_UART_RX, pins::MC_UART_TX);

  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)pins::CAN_TX, (gpio_num_t)pins::CAN_RX, TWAI_MODE_NORMAL);
  twai_timing_config_t  t = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t  f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  g_canOk = twai_driver_install(&g, &t, &f) == ESP_OK && twai_start() == ESP_OK;
  log_i("CAN %s", g_canOk ? "started" : "init failed");
}

void task(void*) {
  for (;;) {
    if (g_canOk) {
      twai_message_t msg;
      while (twai_receive(&msg, 0) == ESP_OK) {
        // TODO(core-04): разбор кадров контроллера мотора (скорость, ток фаз, ошибки).
      }
    }
    while (Serial1.available()) {
      (void)Serial1.read();  // TODO(core-04): парсер UART-протокола контроллера мотора.
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
}  // namespace comms
