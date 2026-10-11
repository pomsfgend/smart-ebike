#ifdef ESP_PLATFORM
#include "charger_link.h"
#include "fc_config.h"
#include "fc_port.h"
#include "esp_now.h"
#include "esp_idf_version.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "CLINK";
static uint8_t s_peer[6];
static SemaphoreHandle_t s_mx;
static fcl_station_t s_st;
static fcl_monitor_t s_mon;
static uint16_t s_seq;
static uint8_t s_chan = FC_WIFI_CHANNEL;
static uint32_t s_hop_ms;

static void parse_hex(const char *s, uint8_t *out, int n)
{
    for (int i = 0; i < n; ++i) { unsigned v = 0; sscanf(s + i * 2 + (s[2] == ':' ? i : 0), "%2x", &v); out[i] = (uint8_t)v; }
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    const uint8_t *mac = info->src_addr;
#else
static void on_recv(const uint8_t *mac, const uint8_t *data, int len)
{
#endif
    if (memcmp(mac, s_peer, 6) != 0 || len <= 0) return;   /* only the paired cube */
    fcl_type_t t; fcl_station_t st;
    if (fcl_unpack(data, (size_t)len, &t, &st, NULL) != FCL_OK || t != FCL_T_STATION) { s_mon.bad++; return; }
    xSemaphoreTake(s_mx, portMAX_DELAY);
    if (fcl_monitor_accept(&s_mon, st.seq, fc_now_ms())) s_st = st;
    xSemaphoreGive(s_mx);
}

esp_err_t clink_init(void)
{
    s_mx = xSemaphoreCreateMutex();
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); nvs_flash_init(); }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* The cube runs a SoftAP on CUBE_WIFI_CHANNEL; ESP-NOW must use the same channel. */
    ESP_ERROR_CHECK(esp_wifi_set_channel(FC_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_set_pmk((const uint8_t *)CONFIG_FC_PMK));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));
    esp_now_peer_info_t p = { .channel = 0 /* current channel */, .ifidx = WIFI_IF_STA, .encrypt = true };
    parse_hex(CONFIG_FC_PEER_MAC, s_peer, 6);
    memcpy(p.peer_addr, s_peer, 6);
    parse_hex(CONFIG_FC_LMK, p.lmk, 16);
    ESP_ERROR_CHECK(esp_now_add_peer(&p));
    FC_LOGI(TAG, "ESP-NOW ch %d, peer %02x:%02x:%02x:%02x:%02x:%02x", FC_WIFI_CHANNEL,
            s_peer[0], s_peer[1], s_peer[2], s_peer[3], s_peer[4], s_peer[5]);
    return ESP_OK;
}

uint32_t clink_station(fcl_station_t *out)
{
    xSemaphoreTake(s_mx, portMAX_DELAY);
    *out = s_st;
    uint32_t age = fcl_monitor_age(&s_mon, fc_now_ms());
    xSemaphoreGive(s_mx);
    return age;
}

void clink_send_request(fcl_request_t *r)
{
    uint8_t buf[FCL_MAX_LEN];
    xSemaphoreTake(s_mx, portMAX_DELAY);
    r->seq = ++s_seq;
    r->ack = s_st.seq;
    xSemaphoreGive(s_mx);
    size_t n = fcl_pack_request(buf, r);
    esp_now_send(s_peer, buf, n);              /* best effort; 10 Hz, freshness by seq/age */
}
void clink_tick(uint32_t now, bool allow_hop)
{
    if (!allow_hop || fcl_monitor_age(&s_mon, now) < 3000u || now - s_hop_ms < 250u) return;
    s_hop_ms = now;
    s_chan = (uint8_t)(s_chan % 13u + 1u);
    esp_wifi_set_channel(s_chan, WIFI_SECOND_CHAN_NONE);
}
#endif
