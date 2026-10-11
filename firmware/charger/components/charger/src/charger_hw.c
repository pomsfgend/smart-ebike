#ifdef ESP_PLATFORM
#include "charger_hw.h"
#include "fc_config.h"
#include "fc_port.h"
#include "jbd_bms.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/twai.h"
#include "driver/uart.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "CHW";
static adc_oneshot_unit_handle_t s_adc;

/* LEDC channel per dynamic driver */
enum { DRV_KBAT = 0, DRV_PERMIT = 1 };
static const int s_drv_pin[] = { FC_PIN_KBAT_DRV, FC_PIN_HV_PERMIT_DRV };

static void drv_set(int ch, bool on)
{
    /* 50 % duty = relay held; 0 % = static low = relay released by the charge pump */
    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch, on ? 512 : 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch);
}

static esp_err_t uart_up(uart_port_t port, int tx, int rx)
{
    uart_config_t c = { .baud_rate = FC_BMS_UART_BAUD, .data_bits = UART_DATA_8_BITS,
                        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
                        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT };
    esp_err_t err = uart_driver_install(port, 256, 0, 0, NULL, 0);
    if (err == ESP_OK) err = uart_param_config(port, &c);
    if (err == ESP_OK) err = uart_set_pin(port, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    return err;
}

esp_err_t chw_init(void)
{
    /* CRPS PS_ON# through an optocoupler: pin HIGH = opto off = PS_ON# pulled up = PSU off */
    gpio_config_t g = { .pin_bit_mask = 1ULL << FC_PIN_CRPS_PSON_N, .mode = GPIO_MODE_OUTPUT };
    gpio_set_level(FC_PIN_CRPS_PSON_N, 1);
    ESP_ERROR_CHECK(gpio_config(&g));
    gpio_config_t b = { .pin_bit_mask = 1ULL << FC_PIN_USER_BTN, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    ESP_ERROR_CHECK(gpio_config(&b));
#if FC_POWER_MODE == FC_POWER_MODE_DIRECT
    gpio_config_t ov = { .pin_bit_mask = 1ULL << FC_PIN_HW_OVP_OK, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    ESP_ERROR_CHECK(gpio_config(&ov));
#endif

    ledc_timer_config_t t = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
                              .timer_num = LEDC_TIMER_0, .freq_hz = FC_DRV_TOGGLE_HZ, .clk_cfg = LEDC_AUTO_CLK };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    for (int i = 0; i < 2; ++i) {
        ledc_channel_config_t c = { .gpio_num = s_drv_pin[i], .speed_mode = LEDC_LOW_SPEED_MODE,
                                    .channel = (ledc_channel_t)i, .timer_sel = LEDC_TIMER_0, .duty = 0 };
        ESP_ERROR_CHECK(ledc_channel_config(&c));
    }

    adc_oneshot_unit_init_cfg_t u = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&u, &s_adc));
    adc_oneshot_chan_cfg_t ac = { .atten = ADC_ATTEN_DB_11, .bitwidth = ADC_BITWIDTH_12 };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, ADC_CHANNEL_0, &ac));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, ADC_CHANNEL_1, &ac));

    ESP_ERROR_CHECK(uart_up(UART_NUM_0, FC_PIN_BMS1_TX, FC_PIN_BMS1_RX));
    ESP_ERROR_CHECK(uart_up(UART_NUM_1, FC_PIN_BMS2_TX, FC_PIN_BMS2_RX));

#if FC_POWER_MODE == FC_POWER_MODE_DCDC
    twai_general_config_t gc = TWAI_GENERAL_CONFIG_DEFAULT(FC_PIN_CAN_TX, FC_PIN_CAN_RX, TWAI_MODE_NORMAL);
    gc.rx_queue_len = 32;
    twai_timing_config_t tc = TWAI_TIMING_CONFIG_125KBITS();
    twai_filter_config_t fc = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    ESP_ERROR_CHECK(twai_driver_install(&gc, &tc, &fc));
    ESP_ERROR_CHECK(twai_start());
    FC_LOGI(TAG, "hw up: 2x JBD UART, Flatpack CAN 125k, drivers off");
#else
    FC_LOGI(TAG, "hw up (DIRECT): 2x JBD UART, Vinlet ADC, HW-OVP input, drivers off");
#endif
    return ESP_OK;
}

void chw_set_outputs(bool kbat, bool hv_permit, bool crps_on)
{
    drv_set(DRV_KBAT, kbat);
    drv_set(DRV_PERMIT, hv_permit);
    gpio_set_level(FC_PIN_CRPS_PSON_N, crps_on ? 0 : 1);
}

uint16_t chw_vstack_0p1v(void)
{
    int raw = 0, acc = 0;
    for (int i = 0; i < 8; ++i) { adc_oneshot_read(s_adc, ADC_CHANNEL_0, &raw); acc += raw; }
    /* TODO: use adc_cali; 12 bit, ~3.1 V full scale (VERIFY); divider FC_VSENSE_DIV_X10 / 10 */
    uint32_t mv = (uint32_t)acc / 8u * 3100u / 4095u;
    return (uint16_t)(mv * FC_VSENSE_DIV_X10 / 1000u);   /* 0.1 V */
}

bool chw_cc2_ok(void)
{
#if CONFIG_FC_CC2_SENSE
    int raw = 0;
    adc_oneshot_read(s_adc, ADC_CHANNEL_1, &raw);
    return raw > 1500 && raw < 3000;          /* window around 6 V via divider (VERIFY) */
#else
    return true;
#endif
}

bool chw_user_button(void) { return gpio_get_level(FC_PIN_USER_BTN) == 0; }

bool chw_hw_ovp_tripped(void)
{
#if FC_POWER_MODE == FC_POWER_MODE_DIRECT
    return gpio_get_level(FC_PIN_HW_OVP_OK) != 0;
#else
    return false;
#endif
}

static esp_err_t transact(uart_port_t port, uint8_t cmd, fc_bms_t *b)
{
    uint8_t req[7], rx[128];
    jbd_build_read(req, cmd);
    uart_flush_input(port);
    uart_write_bytes(port, req, sizeof(req));
    size_t n = 0;
    uint32_t t0 = fc_now_ms();
    while (fc_now_ms() - t0 < 120u && n < sizeof(rx)) {
        int r = uart_read_bytes(port, rx + n, sizeof(rx) - n, pdMS_TO_TICKS(10));
        if (r > 0) n += (size_t)r;
        jbd_result_t res = jbd_parse(rx, n, b, fc_now_ms());
        if (res == JBD_OK) return ESP_OK;
        if (res != JBD_NEED_MORE) return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t chw_bms_poll(int idx, fc_bms_t *b)
{
    uart_port_t port = idx == 0 ? UART_NUM_0 : UART_NUM_1;
    esp_err_t err = transact(port, JBD_CMD_BASIC, b);
    if (err == ESP_OK) err = transact(port, JBD_CMD_CELLS, b);
    if (err != ESP_OK) FC_LOGW(TAG, "BMS%d: %s", idx + 1, esp_err_to_name(err));
    return err;
}

esp_err_t chw_can_send(const fp2_frame_t *f)
{
    twai_message_t m = { .extd = 1, .identifier = f->id, .data_length_code = f->dlc };
    memcpy(m.data, f->d, 8);
    return twai_transmit(&m, pdMS_TO_TICKS(10));
}

bool chw_can_recv(uint32_t *id, uint8_t d[8], uint8_t *dlc, uint32_t timeout_ms)
{
    twai_message_t m;
    if (twai_receive(&m, pdMS_TO_TICKS(timeout_ms)) != ESP_OK || !m.extd) return false;
    *id = m.identifier; *dlc = m.data_length_code;
    memcpy(d, m.data, 8);
    return true;
}
#endif
