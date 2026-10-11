/* Cube side of the FCL link. See fc_cube_link.h. Target: ESP-IDF 5.0.9, ESP32-C3 (the cube). */
#include "fc_cube_link.h"
#include "app_state.h"         /* cube: cube_state_snapshot_t, app_state_set_bcl/... */
#include "config.h"            /* cube: CUBE_WIFI_CHANNEL                            */
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_idf_version.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#ifndef FC_CUBE_PEER_MAC        /* TODO: move into settings.c / web UI (NVS) */
#define FC_CUBE_PEER_MAC  { 0, 0, 0, 0, 0, 0 }
#endif
#ifndef FC_CUBE_PMK
#define FC_CUBE_PMK       "CHANGE_ME_PMK_16"
#endif
#ifndef FC_CUBE_LMK
#define FC_CUBE_LMK       { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff }
#endif
#define FC_CUBE_BHM_VMAX_DEFAULT 1000u   /* direct mode default; DCDC mode sends 2750 over FCL */

static const char *TAG = "FCLINK";
static const uint8_t s_peer[6] = FC_CUBE_PEER_MAC;
static bool s_enabled;
static SemaphoreHandle_t s_mx;
static fcl_request_t s_req;
static fcl_monitor_t s_mon;
static uint16_t s_seq;
static uint32_t s_last_tx;
static uint16_t s_applied_seq;

static uint32_t age(uint32_t now) { return fcl_monitor_age(&s_mon, now); }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    const uint8_t *mac = info->src_addr;
#else
static void on_recv(const uint8_t *mac, const uint8_t *data, int len)
{
#endif
    if (memcmp(mac, s_peer, 6) != 0 || len <= 0) return;
    fcl_type_t t; fcl_request_t r;
    if (fcl_unpack(data, (size_t)len, &t, NULL, &r) != FCL_OK || t != FCL_T_REQUEST) return;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
    xSemaphoreTake(s_mx, portMAX_DELAY);
    if (fcl_monitor_accept(&s_mon, r.seq, now)) s_req = r;
    xSemaphoreGive(s_mx);
}

bool fc_cube_link_enabled(void) { return s_enabled; }

esp_err_t fc_cube_link_init(void)
{
    static const uint8_t zero[6] = {0};
    if (memcmp(s_peer, zero, 6) == 0) { ESP_LOGW(TAG, "no charger paired, link off (stock behaviour)"); return ESP_OK; }
    s_mx = xSemaphoreCreateMutex();
    esp_err_t e = esp_now_init();
    if (e == ESP_OK) e = esp_now_set_pmk((const uint8_t *)FC_CUBE_PMK);
    if (e == ESP_OK) e = esp_now_register_recv_cb(on_recv);
    if (e == ESP_OK) {
        esp_now_peer_info_t p = { .channel = 0 /* = current AP channel */, .ifidx = WIFI_IF_AP, .encrypt = true };
        static const uint8_t lmk[16] = FC_CUBE_LMK;
        memcpy(p.peer_addr, s_peer, 6);
        memcpy(p.lmk, lmk, 16);
        e = esp_now_add_peer(&p);
    }
    s_enabled = e == ESP_OK;
    ESP_LOGI(TAG, "ESP-NOW link %s", s_enabled ? "on" : esp_err_to_name(e));
    return e;
}

static uint8_t map_session(uint8_t main_state)
{
    switch (main_state) {
    case ST_HANDSHAKE_INIT: case ST_WAIT_CRM: return FCL_SESS_HANDSHAKE;
    case ST_PARAM_CONFIG:   return FCL_SESS_PARAM;
    case ST_READY_WAIT_CRO: return FCL_SESS_READY;
    case ST_CHARGING:       return FCL_SESS_CHARGING;
    case ST_STOP: case ST_FINALIZE: return FCL_SESS_STOPPING;
    default:                return FCL_SESS_IDLE;
    }
}

static uint16_t mag(int32_t v) { return (uint16_t)(v < 0 ? -v : v); }
static bool fresh(uint32_t ts, uint32_t now, uint32_t win) { return ts != 0 && now - ts < win; }

static void send_station(uint32_t now)
{
    cube_state_snapshot_t st;
    app_state_snapshot(&st);
    fcl_station_t s = {0};
    s.seq = ++s_seq;
    s.ack = s_req.seq;
    s.session = map_session(st.main_state);
    s.protocol = st.protocol;
    if (fresh(st.last_charger_rx_ms, now, 1000)) s.flags |= FCL_SF_CAN_ALIVE;
    if (fresh(st.last_cml_ms, now, 5000))        s.flags |= FCL_SF_CML_VALID;
    if (fresh(st.last_ccs_ms, now, 1000))        s.flags |= FCL_SF_CCS_VALID;
    if (st.ccs_charge_permit == 1)               s.flags |= FCL_SF_CCS_PERMIT;   /* permit bits == 01 (2011: always 1) */
    if (st.cro_status_raw == 0xAA)               s.flags |= FCL_SF_CRO_READY;
    if (st.relay_a)                              s.flags |= FCL_SF_RELAY_A;
    if (st.relay_b)                              s.flags |= FCL_SF_RELAY_B;
    if (fresh(st.last_cst_ms, now, 2000) || fresh(st.last_cem_ms, now, 2000) ||
        st.charger_stage == CUBE_STAGE_COMPLETED || st.charger_stage == CUBE_STAGE_ERROR)
        s.flags |= FCL_SF_STATION_END;
    s.cst_reason = st.cst_status_raw;
    s.cml_vmax = st.cml_max_voltage_raw;
    s.cml_vmin = st.cml_min_voltage_raw;
    uint16_t a = mag(st.cml_max_current_0p1a), b = mag(st.cml_min_current_0p1a);
    s.cml_imax = a > b ? a : b;
    s.cml_imin = a > b ? b : a;
    s.ccs_v = st.ccs_output_voltage_raw;
    s.ccs_i = mag(st.ccs_output_current_0p1a);
    s.uptime_ms = now;
    uint8_t buf[FCL_MAX_LEN];
    size_t n = fcl_pack_station(buf, &s);
    (void)esp_now_send(s_peer, buf, n);
}

void fc_cube_link_tick(uint32_t now)
{
    if (!s_enabled) return;
    if (now - s_last_tx >= FCL_PERIOD_MS) { s_last_tx = now; send_station(now); }
    fcl_request_t r;
    xSemaphoreTake(s_mx, portMAX_DELAY);
    r = s_req;
    xSemaphoreGive(s_mx);
    if (age(now) > FCL_PAUSE_MS) {
        /* pause: ask the station for 0 A until the charger is back (or the stop timeout hits) */
        app_state_set_bcl(r.bcl_v ? r.bcl_v : 2500, 0, r.bcl_mode ? r.bcl_mode : 1);
        return;
    }
    if (r.seq == s_applied_seq) return;
    s_applied_seq = r.seq;
    /* cube convention: BCL current stored negative = charge (see web.c setpoints_post) */
    app_state_set_bcl(r.bcl_v, (int16_t)-(int16_t)r.bcl_i, r.bcl_mode);
    app_state_set_battery_vi(r.bcs_v, (int16_t)-(int16_t)r.bcs_i);   /* tx_bcs uses |I| */
    app_state_set_soc(r.soc);
}

uint32_t fc_cube_req_age(uint32_t now) { return s_enabled ? age(now) : UINT32_MAX; }

bool fc_cube_ready_for_bro(uint32_t now)
{
    return s_enabled && age(now) <= FCL_PAUSE_MS && (s_req.flags & FCL_RF_READY) && !(s_req.flags & FCL_RF_STOP);
}

bool fc_cube_charge_allowed(uint32_t now)
{
    return s_enabled && age(now) <= FCL_PAUSE_MS && (s_req.flags & FCL_RF_ALLOW);
}

bool fc_cube_stop_requested(uint32_t now, uint8_t bst[4])
{
    if (!s_enabled) return false;
    memset(bst, 0, 4);
    if (age(now) > FCL_STOP_MS) {             /* link lost: "other fault" */
        /* fault word (bytes 2-3, LE) bits 15-16 = 01 "other fault" -> byte 3 = 0x40.
           Same as the factory firmware's fixed BST 40 00 40 00 (docs/PROTOCOL_LIMITS.md §3). */
        bst[1] = 0x00; bst[2] = 0x40;
        return true;
    }
    if (!(s_req.flags & FCL_RF_STOP)) return false;
    bst[0] = s_req.bst_reason ? s_req.bst_reason : 0x01;
    bst[1] = (uint8_t)s_req.bst_fault; bst[2] = (uint8_t)(s_req.bst_fault >> 8);
    bst[3] = s_req.bst_error;
    return true;
}

bool fc_cube_fill_bsm(uint32_t now, uint8_t d[7])
{
    if (!s_enabled || age(now) > FCL_PAUSE_MS) return false;
    const fcl_request_t *r = &s_req;
    uint16_t bits = (uint16_t)r->bsm_byte5;            /* 6 x 2-bit status fields, 00 = normal */
    if (r->flags & FCL_RF_ALLOW) bits |= (1u << 12);    /* charge allowed (01) */
    bits |= (3u << 14);                                 /* reserved = 1 */
    d[0] = r->cell_vmax_no ? r->cell_vmax_no : 1;
    d[1] = (uint8_t)(r->tmax_c + 50);
    d[2] = r->tmax_no ? r->tmax_no : 1;
    d[3] = (uint8_t)(r->tmin_c + 50);
    d[4] = r->tmin_no ? r->tmin_no : 1;
    d[5] = (uint8_t)bits; d[6] = (uint8_t)(bits >> 8);
    return true;
}

/* DIRECT mode (FCL v2, REQUEST.flags & FCL_RF_DIRECT): the 13-byte BCP comes from the charger.
   Returns false if the charger has not asked for it (keep the cube's own BCP). */
bool fc_cube_fill_bcp(uint32_t now, uint8_t b[13])
{
    if (!s_enabled || age(now) > FCL_PAUSE_MS || !(s_req.flags & FCL_RF_DIRECT)) return false;
    const fcl_request_t *r = &s_req;
    uint16_t imax = (uint16_t)(4000u - (r->bcp_imax > 4000u ? 4000u : r->bcp_imax));   /* -400 A offset */
    uint16_t soc01 = (uint16_t)r->soc * 10u;
    b[0] = (uint8_t)r->bcp_cell_cv; b[1] = (uint8_t)(r->bcp_cell_cv >> 8);     /* 0.01 V        */
    b[2] = (uint8_t)imax;           b[3] = (uint8_t)(imax >> 8);
    b[4] = 0x50; b[5] = 0x00;       /* rated energy 8.0 kWh (0.1 kWh/bit) VERIFY per pack     */
    b[6] = (uint8_t)r->bhm_vmax;    b[7] = (uint8_t)(r->bhm_vmax >> 8);         /* max total V   */
    b[8] = (uint8_t)(55 + 50);      /* max allowed battery temperature 55 C                     */
    b[9] = (uint8_t)soc01;          b[10] = (uint8_t)(soc01 >> 8);
    b[11] = (uint8_t)r->batt_v;     b[12] = (uint8_t)(r->batt_v >> 8);          /* REAL pack V   */
    return true;
}

uint16_t fc_cube_bhm_vmax(void)
{
    return (s_enabled && s_req.bhm_vmax) ? s_req.bhm_vmax : FC_CUBE_BHM_VMAX_DEFAULT;
}
