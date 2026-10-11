/*
 * BENCH firmware for the fast-charge add-on, WITHOUT a GB/T station.
 *
 * Purpose: bring up BMS link, DC-DC references/telemetry, contactor + precharge, and the CC/CV
 * supervisor using a current-limited LAB HV supply (e.g. 300-400 V, 1-2 A limit first!)
 * in place of the station. The station is simulated by a fixed CML/CCS.
 *
 * In the product the spoof (FGMDCharge) owns app_main, TWAI and the GB/T state machine and calls
 * fc_start() + fc_on_* hooks; this file is NOT used there.
 *
 * SAFETY: high voltage. Read README.md "Безопасность" before powering the HV side.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "fc_api.h"
#include "fc_config.h"
#include "gbt_codec.h"

static const char *TAG = "FC_BENCH";

/* Simulated station: CML 200..750 V, 0..33 A; CCS permit = 1. */
static void feed_fake_station(void)
{
    uint8_t cml[8];
    uint16_t vmax = 7500, vmin = 2000, imax = gbtc_i_to_raw(330), imin = gbtc_i_to_raw(0);
    cml[0] = vmax; cml[1] = vmax >> 8; cml[2] = vmin; cml[3] = vmin >> 8;
    cml[4] = imax; cml[5] = imax >> 8; cml[6] = imin; cml[7] = imin >> 8;
    fc_on_can_rx(GBTC_ID_CML, cml, 8);
    uint8_t ccs[8] = { 0xA0, 0x0F, 0xA0, 0x0F, 0, 0, 0x01, 0xFF }; /* 400.0 V, 0 A, permit */
    fc_on_can_rx(GBTC_ID_CCS, ccs, 8);
}

void app_main(void)
{
    ESP_LOGW(TAG, "fast-charge BENCH, no GB/T. HV from lab supply only.");
    if (fc_start() != ESP_OK) {
        ESP_LOGE(TAG, "fc_start failed, staying safe (DC-DC off, contactor open)");
        return;
    }
    fc_on_session_state(FC_SESS_HANDSHAKE);
    feed_fake_station();
    fc_on_session_state(FC_SESS_PARAM);
    fc_on_session_state(FC_SESS_READY);

    /* Wait for the battery contactor (precharge + K-BAT) as the spoof would before BRO=AA. */
    for (int i = 0; i < 100 && !fc_ready_for_bro(); ++i) vTaskDelay(pdMS_TO_TICKS(100));
    if (!fc_ready_for_bro()) {
        ESP_LOGE(TAG, "not ready (BMS? CC2 jumper? DC-DC?), abort");
        fc_on_session_state(FC_SESS_IDLE);
        return;
    }
    fc_on_session_state(FC_SESS_CHARGING);

    for (;;) {
        feed_fake_station();                     /* keeps CCS fresh (1 s timeout) */
        fc_status_t st;
        fc_get_status(&st);
        uint8_t r; uint16_t f; uint8_t e;
        if (fc_stop_requested(&r, &f, &e)) {
            ESP_LOGW(TAG, "stop: %s", fc_stop_reason_name(st.out.stop));
            fc_on_session_state(FC_SESS_IDLE);
            break;
        }
        ESP_LOGI(TAG, "Iref %u.%u A Vin %u V Vout %u.%u V cellmax %u mV soc %u%%",
                 st.out.dcdc.iref_0p1a / 10, st.out.dcdc.iref_0p1a % 10, st.dcdc.vin_0p1v / 10,
                 st.dcdc.vout_0p1v / 10, st.dcdc.vout_0p1v % 10, st.out.lim.cell_max_mv, st.out.lim.soc_pct);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
