#pragma once
/* ESP-IDF hardware glue of the charger controller (ESP32-C3 SuperMini). See docs/HARDWARE.md. */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "fc_types.h"
#include "flatpack2.h"

esp_err_t chw_init(void);
/* Coil drivers are "dynamic": a 1 kHz square wave on the pin keeps the relay closed through an
   AC-coupled charge pump. A hung CPU / static pin level opens the relay (hardware watchdog). */
void chw_set_outputs(bool kbat, bool hv_permit, bool crps_on);
uint16_t chw_vstack_0p1v(void);
bool chw_cc2_ok(void);                       /* true if CC2 sense is not fitted (CONFIG)   */
bool chw_user_button(void);
bool chw_hw_ovp_tripped(void);            /* DIRECT: hardware OVP comparator (fail-safe input) */
esp_err_t chw_bms_poll(int idx, fc_bms_t *b);
esp_err_t chw_can_send(const fp2_frame_t *f);
bool chw_can_recv(uint32_t *id, uint8_t d[8], uint8_t *dlc, uint32_t timeout_ms);
