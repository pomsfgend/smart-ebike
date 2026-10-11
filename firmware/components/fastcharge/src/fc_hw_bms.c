/*
 * Two JBD BMSs on one UART1 (GPIO0 TX / GPIO3 RX), switched by TCA9554 P4 through two digital
 * isolators (e.g. ISO7721) whose outputs are enabled alternately. Each JBD UART is referenced
 * to its own pack minus, therefore the isolation is mandatory (see docs/HARDWARE.md 5).
 * Alternative if the JBD has RS485: one RS485 bus is NOT possible with stock JBD firmware
 * (no addressing, VERIFY) -> keep the mux.
 */
#ifdef ESP_PLATFORM
#include "fc_hw.h"
#include "fc_config.h"
#include "fc_port.h"
#include "jbd_bms.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "FC_BMS";
esp_err_t fc_io_bit(uint8_t bit, bool on);

esp_err_t fc_bms_init(void)
{
    uart_config_t c = { .baud_rate = FC_BMS_UART_BAUD, .data_bits = UART_DATA_8_BITS,
                        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
                        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT };
    esp_err_t err = uart_driver_install(FC_BMS_UART_NUM, 256, 0, 0, NULL, 0);
    if (err == ESP_OK) err = uart_param_config(FC_BMS_UART_NUM, &c);
    if (err == ESP_OK) err = uart_set_pin(FC_BMS_UART_NUM, FC_BMS_UART_TX_GPIO, FC_BMS_UART_RX_GPIO,
                                          UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    return err;
}

static esp_err_t transact(uint8_t cmd, fc_bms_t *b)
{
    uint8_t req[7], rx[128];
    jbd_build_read(req, cmd);
    uart_flush_input(FC_BMS_UART_NUM);
    uart_write_bytes(FC_BMS_UART_NUM, req, sizeof(req));
    size_t n = 0;
    uint32_t t0 = fc_now_ms();
    while (fc_now_ms() - t0 < 120u && n < sizeof(rx)) {
        int r = uart_read_bytes(FC_BMS_UART_NUM, rx + n, sizeof(rx) - n, pdMS_TO_TICKS(10));
        if (r > 0) n += (size_t)r;
        jbd_result_t res = jbd_parse(rx, n, b, fc_now_ms());
        if (res == JBD_OK) return ESP_OK;
        if (res != JBD_NEED_MORE) return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t fc_bms_poll(int idx, fc_bms_t *b)
{
    esp_err_t err = fc_io_bit(4, idx == 1);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(2));                 /* isolator enable settle */
    err = transact(JBD_CMD_BASIC, b);
    if (err == ESP_OK) err = transact(JBD_CMD_CELLS, b);
    if (err != ESP_OK) FC_LOGW(TAG, "BMS%d: %s", idx + 1, esp_err_to_name(err));
    return err;
}
#endif
