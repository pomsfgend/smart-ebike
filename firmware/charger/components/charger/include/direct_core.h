#pragma once
/*
 * DIRECT mode charger core (no DC-DC). Pure C, host-tested. docs/DIRECT_MODE.md.
 *
 *   station DC+/DC- --(cube K5/K6)-- inlet --[K-BAT]--[fuses]-- 2 x JBD 18S LiFePO4 (parallel)
 *
 * The station is a current source; the battery clamps the voltage. We:
 *   - keep K-BAT OPEN through the station insulation check (CHM/BHM ... CRM),
 *   - close K-BAT after the station discharged its output (inlet <= 60 V), verify the inlet
 *     equals the pack voltage, then tell the cube READY (-> BRO 0xAA),
 *   - request BCL: V = max(65.7 V, CML min) <= BCP max, mode CC, I = our CC/CV/taper result,
 *   - BCP: max current 100 A, max total V = FC_D_BCP_VMAX, cell 3.65 V, battery V = real,
 *   - independent checks: pack/cell OV, OCP vs our limit, station overshoot, link loss,
 *     hardware OVP input; stop = BCL I = 0 + BST, open K-BAT after |I| < 1 A or timeout.
 */
#include <stdbool.h>
#include <stdint.h>
#include "fc_types.h"
#include "fc_link.h"
#include "fc_supervisor.h"

typedef enum { DS_IDLE = 0, DS_ARMED, DS_INSUL, DS_CLOSE, DS_CHARGING, DS_RAMPDOWN, DS_DONE } ds_state_t;

typedef struct {
    const fc_bms_t *bms;          /* [2]                                                  */
    const fcl_station_t *st;      /* last STATION frame (NULL if never)                    */
    uint32_t link_age_ms;
    uint16_t vinlet_0p1v;         /* ADC, inlet side of K-BAT (= station output)           */
    bool     hw_ovp;              /* hardware OVP comparator output (true = tripped)       */
    bool     user_stop;
    bool     cc2_ok;
    bool     enable;              /* user armed fast charge (button / web)                 */
    uint16_t user_i_max_0p1a;     /* 0 = default FC_D_I_USER_MAX_0P1A                      */
} ds_in_t;

typedef struct {
    bool kbat;                    /* battery contactor coil                               */
    bool hv_permit;               /* permit relay in series with cube K5/K6 coil           */
    fcl_request_t req;            /* seq/ack filled by the transport                       */
    fc_limits_t lim;
} ds_out_t;

typedef struct {
    ds_state_t state;
    uint32_t state_ms;
    uint32_t inlet_ok_since;      /* 0 = not yet                                          */
    uint16_t i_req_0p1a;          /* current BCL request                                  */
    uint32_t last_ms;
    uint32_t ocp_since, overshoot_since, term_since, minI_since;
    bool     hard_stop;           /* rampdown with short timeout                          */
    fc_stop_reason_t stop;
} direct_core_t;

void direct_core_init(direct_core_t *c);
void direct_core_step(direct_core_t *c, const ds_in_t *in, uint32_t now, ds_out_t *o);
const char *direct_state_name(ds_state_t s);

/* Pure helpers (exported for tests) */
uint16_t direct_bcl_voltage(const fcl_station_t *st);       /* 0 = station incompatible */
int32_t  direct_batt_current_0p1a(const fc_bms_t bms[2]);    /* + = charge, sum of packs */
