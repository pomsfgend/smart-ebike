#pragma once
/*
 * Fast-charge supervisor: the "brain" that sits between the GB/T session (spoof) and the
 * DC-DC converter. Pure C, deterministic, host-tested. Called every FC_TASK_PERIOD_MS.
 *
 * Source of truth = the two JBD BMSs. The station only provides HV power; its CML/CCS
 * values are used to choose a request it can actually deliver.
 *
 *   BMS x2 ──> per-pack limit (temp derate, cell taper, faults)
 *          ──> total current limit (current sharing between parallel packs)
 *          ──> DC-DC Iref (slew limited) + Vref (65.7 V)  [fast loop is in DC-DC hardware]
 *          ──> station request: BCL V = 400 V (in CML range), I = Pin/V * 1.15
 */
#include <stdbool.h>
#include <stdint.h>
#include "fc_types.h"

typedef struct {
    uint16_t lim_0p1a[2];           /* per pack                 */
    uint16_t total_lim_0p1a;
    fc_stop_reason_t stop;          /* != NONE -> stop charging */
    uint16_t cell_max_mv, cell_min_mv;
    uint8_t  cell_max_no;           /* 1..36 over both packs    */
    int16_t  tmax_dc, tmin_dc;
    uint8_t  soc_pct;               /* capacity-weighted        */
    uint16_t pack_0p1v;             /* highest pack voltage     */
} fc_limits_t;

void fc_limits_compute(const fc_bms_t bms[2], uint32_t now_ms, fc_limits_t *out);
uint16_t fc_temp_derate_pct(int16_t t_dc);
uint16_t fc_cell_taper_0p1a(uint16_t cell_max_mv, uint16_t i_max_0p1a);

typedef struct {
    uint16_t iref_0p1a;             /* slew-limited command     */
    uint32_t last_ms;
    uint32_t term_since_ms;         /* 0 = not in termination window */
    fc_stop_reason_t stop;          /* latched                  */
    bool     running;
} fc_supervisor_t;

typedef struct {
    const fc_bms_t      *bms;       /* [2] */
    const fc_dcdc_tel_t *dcdc;
    const fc_station_t  *station;
    bool cc2_ok;
    bool user_stop;
    bool power_path_ready;          /* contactors closed, station delivering (CCS permit) */
} fc_sup_in_t;

typedef struct {
    fc_dcdc_cmd_t   dcdc;
    fc_hv_request_t hv;
    fc_limits_t     lim;
    fc_stop_reason_t stop;
} fc_sup_out_t;

void fc_supervisor_init(fc_supervisor_t *s);
void fc_supervisor_start(fc_supervisor_t *s, uint32_t now_ms);
void fc_supervisor_step(fc_supervisor_t *s, const fc_sup_in_t *in, uint32_t now_ms, fc_sup_out_t *out);

/* Station voltage/current request for BCL (also used before charging for BCP/BHM sanity). */
fc_hv_request_t fc_hv_request(const fc_station_t *st, uint16_t pout_w);
/* Can this station be used at all (CML window overlaps the DC-DC input window)? */
bool fc_station_compatible(const fc_station_t *st);
