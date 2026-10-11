#pragma once
/*
 * >>> THE INTEGRATION INTERFACE WITH THE SPOOF (FGMDCharge-FD-ROUND-V2, ESP-IDF, ESP32-C3) <<<
 * See docs/INTEGRATION_WITH_SPOOF.md for the exact call sites (file / function / line context).
 *
 * The spoof keeps owning: TWAI driver, protocol auto-detect, the GB/T session state machine
 * (sm_relay.c), the UI/web. The fast-charge component (FC) only:
 *   - receives every charger frame          -> fc_on_can_rx()
 *   - is told about session state changes   -> fc_on_session_state()
 *   - decides whether BRO may become 0xAA   -> fc_ready_for_bro()
 *   - fills the payload of BHM/BRM/BCP/BCL/BCS/BSM/BST/BSD with REAL data (from the BMSs and
 *     the DC-DC), replacing the constants in sm_relay.c tx_*()
 *   - asks for a session stop with a reason -> fc_stop_requested()
 *   - drives the DC-DC and the battery-side contactor itself (fc_task)
 *
 * All functions are thread-safe (internal lock) and non-blocking.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fc_types.h"
#include "fc_supervisor.h"

typedef enum {
    FC_SESS_IDLE = 0,       /* ST_IDLE, ST_STOP, ST_FINALIZE                       */
    FC_SESS_HANDSHAKE,      /* ST_HANDSHAKE_INIT, ST_WAIT_CRM (CHM/BHM, CRM/BRM)   */
    FC_SESS_PARAM,          /* ST_PARAM_CONFIG (BCP / CML / CTS)                   */
    FC_SESS_READY,          /* ST_READY_WAIT_CRO (BRO / CRO)                       */
    FC_SESS_CHARGING,       /* ST_CHARGING (BCL BCS BSM / CCS)                     */
} fc_session_t;

/* lifecycle */
void fc_core_init(void);                         /* pure state init (host + ESP)   */
#ifdef ESP_PLATFORM
#include "esp_err.h"
esp_err_t fc_start(void);                        /* ESP: HW init + fc_task          */
#endif

/* inputs from the spoof */
void fc_on_can_rx(uint32_t id, const uint8_t *d, uint8_t dlc);
void fc_on_session_state(fc_session_t s);
void fc_user_stop(void);

/* decisions for the spoof */
bool fc_ready_for_bro(void);                     /* battery side precharged & contactor closed */
bool fc_stop_requested(uint8_t *bst_reason, uint16_t *bst_fault, uint8_t *bst_error);

/* payload fillers; return length, 0 = "use the spoof default" */
size_t fc_fill_bhm(uint8_t *d);
size_t fc_fill_brm(uint8_t *d, bool proto_2011);
size_t fc_fill_bcp(uint8_t *d);
size_t fc_fill_bcl(uint8_t *d);
size_t fc_fill_bcs(uint8_t *d);
size_t fc_fill_bsm(uint8_t *d);
size_t fc_fill_bsd(uint8_t *d);

/* inputs from the fc HW drivers (also used by host tests to inject data) */
void fc_set_bms(int idx, const fc_bms_t *b);
void fc_set_dcdc(const fc_dcdc_tel_t *t);
void fc_set_cc2(bool ok, uint16_t mv);
void fc_set_batt_contactor(bool closed);

/* one supervisor step, called from fc_task every FC_TASK_PERIOD_MS (or by host tests) */
void fc_core_step(uint32_t now_ms, fc_dcdc_cmd_t *dcdc_cmd, bool *close_batt_contactor);

/* status for UI/web (copy) */
typedef struct {
    fc_session_t session;
    fc_station_t station;
    fc_sup_out_t out;
    fc_dcdc_tel_t dcdc;
    bool cc2_ok; uint16_t cc2_mv;
    bool batt_contactor;
} fc_status_t;
void fc_get_status(fc_status_t *o);
