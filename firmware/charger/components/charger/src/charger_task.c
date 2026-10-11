#ifdef ESP_PLATFORM
/*
 * Charger controller tasks:
 *   bms_task  : JBD #1 / #2 alternately (UART0 / UART1)
 *   can_task  : Flatpack2 RX, login keep-alive, default-voltage programming
 *   ctrl_task : 10 Hz charger_core_step -> outputs, Flatpack setpoint, REQUEST to the cube
 */
#include "charger_core.h"
#include "charger_hw.h"
#include "charger_link.h"
#include "fc_config.h"
#include "fc_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "CHG";
static SemaphoreHandle_t s_mx;
static fc_bms_t s_bms[2];
static fp2_t s_fp[FC_STACK_STRINGS_MAX];
static bool s_fp_default_done[FC_STACK_STRINGS_MAX];

void fc_lock(void) { xSemaphoreTake(s_mx, portMAX_DELAY); }
void fc_unlock(void) { xSemaphoreGive(s_mx); }

static void bms_task(void *arg)
{
    (void)arg;
    for (int k = 0;; k ^= 1) {
        fc_bms_t b;
        fc_lock(); b = s_bms[k]; fc_unlock();
        if (chw_bms_poll(k, &b) == ESP_OK) { fc_lock(); s_bms[k] = b; fc_unlock(); }
        vTaskDelay(pdMS_TO_TICKS(FC_BMS_POLL_MS));
    }
}

static int fp_slot(uint32_t id, const uint8_t *d, uint8_t dlc)
{
    if ((id & 0x00FF0000u) == 0 && dlc >= 7 && d[0] == 0x1B) {          /* hello: match serial */
        for (int k = 0; k < FC_STACK_STRINGS; ++k)
            if (s_fp[k].seen && memcmp(s_fp[k].serial, &d[1], 6) == 0) return k;
        for (int k = 0; k < FC_STACK_STRINGS; ++k) if (!s_fp[k].seen) { s_fp[k].id = (uint8_t)(k + 1); return k; }
        return -1;
    }
    int id8 = (int)((id >> 16) & 0xFF);
    return (id8 >= 1 && id8 <= FC_STACK_STRINGS) ? id8 - 1 : -1;
}

static void can_task(void *arg)
{
    (void)arg;
    uint32_t last_login = 0;
    for (;;) {
        uint32_t id; uint8_t d[8], dlc;
        if (chw_can_recv(&id, d, &dlc, 50)) {
            fc_lock();
            int k = fp_slot(id, d, dlc);
            if (k >= 0) {
                uint8_t keep_id = s_fp[k].id;
                fp2_on_rx(&s_fp[k], id, d, dlc, fc_now_ms());
                if (keep_id) s_fp[k].id = keep_id;
            }
            fc_unlock();
        }
        uint32_t now = fc_now_ms();
        if (now - last_login >= FC_FP_LOGIN_PERIOD_MS) {
            last_login = now;
            for (int k = 0; k < FC_STACK_STRINGS; ++k) {
                fp2_frame_t f; bool seen, do_def;
                fc_lock(); seen = s_fp[k].seen; do_def = seen && !s_fp_default_done[k];
                if (seen) fp2_build_login(&f, &s_fp[k]);
                fc_unlock();
                if (!seen) continue;
                chw_can_send(&f);
                if (do_def) {                       /* fallback voltage if we ever stop talking */
                    fc_lock(); fp2_build_default_v(&f, &s_fp[k], FC_FP_DEFAULT_V_CV); s_fp_default_done[k] = true; fc_unlock();
                    chw_can_send(&f);
                    FC_LOGI(TAG, "FP%d: default voltage %u cV programmed", k + 1, FC_FP_DEFAULT_V_CV);
                }
            }
        }
    }
}

static void ctrl_task(void *arg)
{
    (void)arg;
    static charger_core_t core;
    charger_core_init(&core);
    uint32_t last_set = 0;
    cs_state_t prev = CS_IDLE;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(FC_TASK_PERIOD_MS));
        uint32_t now = fc_now_ms();
        cc_in_t in;
        memset(&in, 0, sizeof(in));
        fc_lock();
        memcpy(in.bms, s_bms, sizeof(in.bms));
        memcpy(in.fp, s_fp, sizeof(s_fp));
        fc_unlock();
        in.fp_count = FC_STACK_STRINGS;
        in.vstack_0p1v = chw_vstack_0p1v();
        in.cc2_ok = chw_cc2_ok();
        in.user_stop = chw_user_button();
        in.link_age_ms = clink_station(&in.st);

        cc_out_t o;
        charger_core_step(&core, &in, now, &o);
        chw_set_outputs(o.kbat, o.hv_permit, o.crps_on);
        if (o.fp_enable && now - last_set >= FC_FP_SET_PERIOD_MS) {
            last_set = now;
            fp2_frame_t f;
            fp2_build_set(&f, o.fp_i_da, o.fp_v_cv, FC_FP_OVP_CV);
            chw_can_send(&f);
        }
        clink_send_request(&o.req);
        clink_tick(now, core.state == CS_IDLE);
        if (core.state != prev) {
            FC_LOGI(TAG, "%s -> %s (stop=%s, Vbat=%u, Vstack=%u)", cs_state_name(prev), cs_state_name(core.state),
                    fc_stop_reason_name(core.stop), o.req.batt_v, in.vstack_0p1v);
            prev = core.state;
        }
    }
}

void charger_start(void)
{
    s_mx = xSemaphoreCreateMutex();
    chw_set_outputs(false, false, false);
    xTaskCreate(bms_task, "bms", 4096, NULL, 5, NULL);
    xTaskCreate(can_task, "fp2", 4096, NULL, 6, NULL);
    xTaskCreate(ctrl_task, "ctrl", 6144, NULL, 7, NULL);
}
#endif
