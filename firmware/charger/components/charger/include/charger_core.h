#pragma once
/*
 * Charger controller core: pure C state machine of the battery-side power path.
 * Inputs: two JBD BMSs, Flatpack2 status, stack voltage ADC, STATION frames from the cube.
 * Outputs: K-BAT, HV permit, CRPS PS_ON, Flatpack setpoint, REQUEST frame for the cube.
 * Hardware glue (charger_hw.c / charger_task.c) only moves data in and out.
 *
 *   IDLE ─session>IDLE─> ARMED ─session CHARGING─> WAIT_HV ─Vin ok─> MATCH ─Vstack≈Vbat─> CHARGING
 *     ^                                                                                    │ stop
 *     └──────────── DONE <── session IDLE ── RAMPDOWN (I->0, K-BAT open, PSUs off) <────────┘
 */
#include <stdbool.h>
#include <stdint.h>
#include "fc_types.h"
#include "fc_link.h"
#include "fc_supervisor.h"
#include "flatpack2.h"
#include "fc_config.h"

typedef enum { CS_IDLE = 0, CS_ARMED, CS_WAIT_HV, CS_MATCH, CS_CHARGING, CS_RAMPDOWN, CS_DONE } cs_state_t;
const char *cs_state_name(cs_state_t s);

typedef struct {
    fc_bms_t      bms[2];
    fp2_t         fp[FC_STACK_STRINGS_MAX];
    uint8_t       fp_count;           /* configured strings (FC_STACK_STRINGS)          */
    uint16_t      vstack_0p1v;        /* ADC, before K-BAT                               */
    bool          cc2_ok;
    bool          user_stop;
    fcl_station_t st;                 /* last accepted STATION frame                     */
    uint32_t      link_age_ms;        /* fcl_monitor_age()                                */
} cc_in_t;

typedef struct {
    bool     kbat;                    /* battery contactor                               */
    bool     hv_permit;               /* series interlock in the K5/K6 coil circuit     */
    bool     crps_on;
    bool     fp_enable;               /* send setpoints (login is always sent if seen)   */
    uint16_t fp_v_cv;                 /* Flatpack voltage setpoint, 0.01 V               */
    uint16_t fp_i_da;                 /* Flatpack current limit PER STRING, 0.1 A        */
    fcl_request_t req;                /* seq/ack filled by the link layer                */
} cc_out_t;

typedef struct {
    cs_state_t       state;
    uint32_t         state_ms;
    fc_supervisor_t  sup;
    fc_sup_out_t     sup_out;
    fc_stop_reason_t stop;            /* latched reason of the last session              */
    fc_dcdc_tel_t    tel;             /* aggregated stack telemetry                      */
    uint16_t         vcrps_0p1v;
} charger_core_t;

void charger_core_init(charger_core_t *c);
void charger_core_step(charger_core_t *c, const cc_in_t *in, uint32_t now_ms, cc_out_t *out);
