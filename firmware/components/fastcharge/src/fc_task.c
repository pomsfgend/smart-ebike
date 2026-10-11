/* ESP-IDF glue: hardware polling + supervisor loop. Low priority vs. the spoof's CAN/SM tasks. */
#ifdef ESP_PLATFORM
#include "fc_api.h"
#include "fc_hw.h"
#include "fc_config.h"
#include "fc_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "FC_TASK";
static SemaphoreHandle_t s_mx;

void fc_lock(void)   { if (s_mx) xSemaphoreTake(s_mx, portMAX_DELAY); }
void fc_unlock(void) { if (s_mx) xSemaphoreGive(s_mx); }

static void bms_task(void *arg)
{
    (void)arg;
    fc_bms_t b[2] = { 0 };
    for (int k = 0;; k ^= 1) {
        if (fc_bms_poll(k, &b[k]) == ESP_OK) fc_set_bms(k, &b[k]);
        vTaskDelay(pdMS_TO_TICKS(FC_BMS_POLL_MS));
    }
}

static void fc_task(void *arg)
{
    (void)arg;
    bool contactor = false;
    for (;;) {
        fc_dcdc_tel_t tel = { 0 };
        (void)fc_dcdc_read(&tel);
        fc_set_dcdc(&tel);
        bool cc2 = false; uint16_t mv = 0;
        (void)fc_cc2_read(&cc2, &mv);
        fc_set_cc2(cc2, mv);

        fc_dcdc_cmd_t cmd; bool want_k = false;
        fc_core_step(fc_now_ms(), &cmd, &want_k);

        /* Order matters: on opening, DC-DC off first, then the contactor (no hot break). */
        if (!want_k || !cmd.enable) (void)fc_dcdc_apply(&(fc_dcdc_cmd_t){ 0 });
        if (want_k != contactor) {
            if (want_k) {
                /* Precharge of the DC-DC output capacitors from the battery through R_pre
                   (TCA9554 P7 -> small relay + 47 R / 50 W, VERIFY): wait until Vout is close
                   to the pack voltage, then close K-BAT and drop the precharge relay. */
                (void)fc_precharge_set(true);
                uint32_t t0 = fc_now_ms();
                bool ok = false;
                while (fc_now_ms() - t0 < 2000u) {
                    fc_dcdc_tel_t t2 = { 0 };
                    fc_status_t st;
                    (void)fc_dcdc_read(&t2);
                    fc_get_status(&st);
                    int32_t dv = (int32_t)st.out.lim.pack_0p1v - (int32_t)t2.vout_0p1v;
                    if (st.out.lim.pack_0p1v > 400 && dv < 20 && dv > -20) { ok = true; break; }  /* < 2 V */
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
                if (!ok) { (void)fc_precharge_set(false); FC_LOGE(TAG, "precharge failed"); vTaskDelay(pdMS_TO_TICKS(FC_TASK_PERIOD_MS)); continue; }
                (void)fc_batt_contactor_set(true);
                vTaskDelay(pdMS_TO_TICKS(50));
                (void)fc_precharge_set(false);
            } else {
                vTaskDelay(pdMS_TO_TICKS(50));  /* let current decay; TODO: wait until I < 1 A */
                (void)fc_batt_contactor_set(false);
            }
            contactor = want_k;
        }
        /* TODO(HW-6): compare fc_batt_contactor_feedback() with 'contactor' -> welding / open fault */
        fc_set_batt_contactor(contactor && fc_batt_contactor_feedback());
        if (want_k && cmd.enable) (void)fc_dcdc_apply(&cmd);
        vTaskDelay(pdMS_TO_TICKS(FC_TASK_PERIOD_MS));
    }
}

esp_err_t fc_start(void)
{
    s_mx = xSemaphoreCreateMutex();
    if (!s_mx) return ESP_ERR_NO_MEM;
    fc_core_init();
    esp_err_t err = fc_i2c_init();
    if (err == ESP_OK) err = fc_dcdc_init();      /* DC-DC disabled, contactor open */
    if (err == ESP_OK) err = fc_bms_init();
    if (err != ESP_OK) { FC_LOGE(TAG, "HW init failed: %s, fast charge disabled", esp_err_to_name(err)); return err; }
    if (xTaskCreate(bms_task, "fc_bms", 3072, NULL, 3, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    if (xTaskCreate(fc_task, "fc_sup", 4096, NULL, 3, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    FC_LOGW(TAG, "fast charge add-on up");
    return ESP_OK;
}
#endif
