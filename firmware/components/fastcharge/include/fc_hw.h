#pragma once
/* ESP-IDF hardware drivers of the fast-charge add-on (ESP32-C3). Not built on the host. */
#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "fc_types.h"

esp_err_t fc_i2c_init(void);                      /* shares I2C0 with the CST816S touch */
esp_err_t fc_dcdc_init(void);                     /* MCP4728 DAC + ADS1115 + TCA9554     */
esp_err_t fc_dcdc_apply(const fc_dcdc_cmd_t *c);  /* write refs + enable                 */
esp_err_t fc_dcdc_read(fc_dcdc_tel_t *t);         /* read Vin/Vout/Iout/T/FAULT          */
esp_err_t fc_cc2_read(bool *ok, uint16_t *mv);    /* CC2 detection point 2 voltage        */
esp_err_t fc_bms_init(void);                      /* UART1 + isolator mux                 */
esp_err_t fc_bms_poll(int idx, fc_bms_t *b);      /* one request/response cycle           */
esp_err_t fc_batt_contactor_set(bool closed);     /* battery-side DC contactor (K-BAT)    */
esp_err_t fc_precharge_set(bool on);              /* precharge relay across K-BAT         */
bool      fc_batt_contactor_feedback(void);       /* aux contact, detects welding         */
#endif
