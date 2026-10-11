/*
 * DC-DC supervisor interface, variant "analog references over I2C" (docs/DCDC_DESIGN.md, A):
 *   MCP4728 ch A = Iref module 1, ch B = Iref module 2, ch C = Vref (both), ch D = spare
 *   ADS1115   A0 = CC2 (via divider), A1 = Vin (isolated amp), A2 = Vout, A3 = heatsink NTC
 *   TCA9554  P0 = EN1, P1 = EN2 (out), P2 = FAULT1, P3 = FAULT2 (in, active low),
 *            P4 = BMS mux select (out), P5 = K-BAT coil (out), P6 = K-BAT aux contact (in),
 *            P7 = precharge relay (out)
 * Output current is NOT read here: it comes from the JBD BMSs (sum) and from the module's
 * own current-sense monitor output if wired (TODO).
 *
 * All scale factors are placeholders: VERIFY / calibrate on the bench (tasks DC-3, HW-4).
 */
#ifdef ESP_PLATFORM
#include "fc_hw.h"
#include "fc_config.h"
#include "fc_port.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "FC_DCDC";

/* Scaling (TODO calibrate): DAC 12 bit, Vref 2.048 V internal. */
#define DAC_FS_MV            2048
#define IREF_MV_PER_A        16      /* 128 A per module -> 2.048 V, VERIFY with power stage */
#define VREF_MV_PER_V        30      /* 65.7 V -> 1.971 V                                     */
#define ADC_VIN_V_PER_MV     0.5f    /* isolated amp: 1 mV at ADC = 0.5 V at HV input, VERIFY */
#define ADC_VOUT_V_PER_MV    0.04f
#define CC2_DIV              4       /* 12 V nominal -> 3 V at ADC                            */

static uint8_t s_io_out = 0;         /* TCA9554 output latch, all off */

static esp_err_t wr(uint8_t addr, const uint8_t *b, size_t n)
{
    return i2c_master_write_to_device(FC_I2C_PORT, addr, b, n, pdMS_TO_TICKS(20));
}
static esp_err_t wrrd(uint8_t addr, uint8_t reg, uint8_t *b, size_t n)
{
    return i2c_master_write_read_device(FC_I2C_PORT, addr, &reg, 1, b, n, pdMS_TO_TICKS(20));
}

esp_err_t fc_i2c_init(void)
{
    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER, .sda_io_num = 1, .scl_io_num = 8,   /* CUBE_TOUCH_SDA/SCL_GPIO */
        .sda_pullup_en = GPIO_PULLUP_ENABLE, .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    esp_err_t err = i2c_param_config(FC_I2C_PORT, &cfg);
    if (err != ESP_OK) return err;
    err = i2c_driver_install(FC_I2C_PORT, cfg.mode, 0, 0, 0);
    return (err == ESP_ERR_INVALID_STATE) ? ESP_OK : err;   /* already installed by touch */
}

static esp_err_t io_write(void)
{
    uint8_t b[2] = { 0x01, s_io_out };
    return wr(FC_I2C_ADDR_IO_TCA9554, b, 2);
}

esp_err_t fc_io_bit(uint8_t bit, bool on)
{
    if (on) s_io_out |= (uint8_t)(1u << bit); else s_io_out &= (uint8_t)~(1u << bit);
    return io_write();
}

esp_err_t fc_dcdc_init(void)
{
    s_io_out = 0;
    esp_err_t err = io_write();                               /* outputs low BEFORE direction */
    uint8_t cfg[2] = { 0x03, 0x4C };                          /* P2,P3,P6 inputs, rest outputs */
    if (err == ESP_OK) err = wr(FC_I2C_ADDR_IO_TCA9554, cfg, 2);
    fc_dcdc_cmd_t off = { 0 };
    if (err == ESP_OK) err = fc_dcdc_apply(&off);
    if (err != ESP_OK) FC_LOGE(TAG, "init failed: %s", esp_err_to_name(err));
    return err;
}

static uint16_t clamp12(uint32_t mv) { uint32_t c = mv * 4095u / DAC_FS_MV; return (uint16_t)(c > 4095 ? 4095 : c); }

esp_err_t fc_dcdc_apply(const fc_dcdc_cmd_t *c)
{
    uint16_t i_mod_0p1a = (uint16_t)(c->iref_0p1a / FC_DCDC_MODULES);
    uint16_t ci = clamp12((uint32_t)i_mod_0p1a * IREF_MV_PER_A / 10u);
    uint16_t cv = clamp12((uint32_t)c->vref_0p1v * VREF_MV_PER_V / 10u);
    if (!c->enable) ci = 0;
    /* MCP4728 "fast write" of 4 channels, internal Vref + gain x1 need the multi-write command;
       TODO(DC-3): use sequential write with VREF bits, this placeholder uses fast write (VDD ref). */
    uint8_t b[8] = { (uint8_t)(ci >> 8), (uint8_t)ci, (uint8_t)(ci >> 8), (uint8_t)ci,
                     (uint8_t)(cv >> 8), (uint8_t)cv, 0, 0 };
    esp_err_t err = wr(FC_I2C_ADDR_DAC_MCP4728, b, sizeof(b));
    /* EN only after references are written; disable goes first in hardware too. */
    s_io_out = (uint8_t)((s_io_out & ~0x03u) | (c->enable ? 0x03u : 0x00u));
    if (err == ESP_OK) err = io_write();
    return err;
}

/* ADS1115 single-shot, FS +-4.096 V, 128 SPS. ~8 ms per channel. */
static esp_err_t ads_read_mv(uint8_t ch, int32_t *mv)
{
    uint16_t cfg = (uint16_t)(0x8000u | ((4u + ch) << 12) | (1u << 9) | (1u << 8) | (4u << 5) | 0x3u);
    uint8_t w[3] = { 0x01, (uint8_t)(cfg >> 8), (uint8_t)cfg };
    esp_err_t err = wr(FC_I2C_ADDR_ADC_ADS1115, w, 3);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(10));
    uint8_t r[2];
    err = wrrd(FC_I2C_ADDR_ADC_ADS1115, 0x00, r, 2);
    if (err != ESP_OK) return err;
    int16_t raw = (int16_t)((r[0] << 8) | r[1]);
    *mv = (int32_t)raw * 4096 / 32768;
    return ESP_OK;
}

esp_err_t fc_cc2_read(bool *ok, uint16_t *mv)
{
    int32_t a;
    esp_err_t err = ads_read_mv(0, &a);
    if (err != ESP_OK) { *ok = false; return err; }
    int32_t v = a * CC2_DIV;
    *mv = (uint16_t)(v < 0 ? 0 : v);
    /* GB/T 18487.1 annex B: detection point 2 = 6 V when connected (R5 1k pull-up to U2=12 V,
       R3 1k in the plug to PE). Window 4.8..7.2 V is a placeholder, VERIFY tolerances. */
    *ok = v >= 4800 && v <= 7200;
    return ESP_OK;
}

esp_err_t fc_dcdc_read(fc_dcdc_tel_t *t)
{
    int32_t vin, vout, tntc;
    esp_err_t err = ads_read_mv(1, &vin);
    if (err == ESP_OK) err = ads_read_mv(2, &vout);
    if (err == ESP_OK) err = ads_read_mv(3, &tntc);
    uint8_t in = 0xFF;
    if (err == ESP_OK) err = wrrd(FC_I2C_ADDR_IO_TCA9554, 0x00, &in, 1);
    if (err != ESP_OK) { t->valid = false; return err; }
    t->vin_0p1v  = (uint16_t)(vin * ADC_VIN_V_PER_MV * 10.0f);
    t->vout_0p1v = (uint16_t)(vout * ADC_VOUT_V_PER_MV * 10.0f);
    t->temp_dc   = (int16_t)(250 + (1650 - tntc) / 2);   /* TODO: real NTC table */
    t->fault     = !(in & 0x04) || !(in & 0x08);
    t->enabled   = (s_io_out & 0x03) == 0x03;
    t->iout_0p1a = 0;                                     /* TODO: module IMON */
    t->valid = true;
    t->rx_ms = fc_now_ms();
    return ESP_OK;
}

esp_err_t fc_batt_contactor_set(bool closed) { return fc_io_bit(5, closed); }
esp_err_t fc_precharge_set(bool on) { return fc_io_bit(7, on); }

bool fc_batt_contactor_feedback(void)
{
    uint8_t in = 0;
    if (wrrd(FC_I2C_ADDR_IO_TCA9554, 0x00, &in, 1) != ESP_OK) return false;
    return (in & 0x40) != 0;
}
#endif
