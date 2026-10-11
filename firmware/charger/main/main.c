/*
 * Charger controller ESP32-C3 (separate board on the bike, next to the PSU stack).
 * Talks to: 2x JBD BMS (UART0/UART1), Eltek Flatpack2 (TWAI 125k via ISO1050),
 * CRPS PS_ON#, battery contactor, HV permit; cube over ESP-NOW (FCL protocol).
 * It never touches the station CAN: the cube stays the only BMS node on GB/T.
 */
#include "charger_hw.h"
#include "charger_link.h"
#include "fc_port.h"

void charger_start(void);

void app_main(void)
{
    ESP_ERROR_CHECK(chw_init());       /* all outputs OFF first */
    ESP_ERROR_CHECK(clink_init());
    charger_start();
    FC_LOGI("MAIN", "charger controller started");
}
