#include "fc_supervisor.h"
#include "fc_config.h"
#include "fc_link.h"
#include <string.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

const char *fc_stop_reason_name(fc_stop_reason_t r)
{
    static const char *n[] = { "none", "full", "cell_ov", "pack_ov", "temp", "bms_fault",
        "bms_lost", "dcdc_fault", "dcdc_lost", "pack_imbalance", "station_range", "cc2_lost", "user", "link_lost", "station_end", "precharge", "overcurrent", "inlet", "hw_ovp" };
    return (unsigned)r < sizeof(n) / sizeof(n[0]) ? n[r] : "?";
}

uint16_t fc_temp_derate_pct(int16_t t)
{
    if (t < FC_T_CHG_MIN_DC) return 0;
    if (t < FC_T_COLD_DC)    return 20;
    if (t < FC_T_COOL_DC)    return 50;
    if (t <= FC_T_WARM_DC)   return 100;
    if (t <= FC_T_HOT_DC)    return 50;
    if (t <= FC_T_CHG_MAX_DC) return 20;
    return 0;
}

uint16_t fc_cell_taper_0p1a(uint16_t vmax, uint16_t imax)
{
    if (vmax <= FC_CELL_TAPER_START_MV) return imax;
    if (vmax >= FC_CELL_CV_MV) return FC_TERM_I_0P1A / FC_PACK_COUNT;
    uint32_t span = FC_CELL_CV_MV - FC_CELL_TAPER_START_MV;
    uint32_t i = (uint32_t)imax * (FC_CELL_CV_MV - vmax) / span;
    return (uint16_t)MAX(i, (uint32_t)(FC_TERM_I_0P1A / FC_PACK_COUNT));
}

void fc_limits_compute(const fc_bms_t bms[2], uint32_t now, fc_limits_t *o)
{
    memset(o, 0, sizeof(*o));
    o->cell_min_mv = 0xFFFF; o->tmin_dc = 1000; o->tmax_dc = -1000;
    uint32_t cap_sum = 0, soc_w = 0;

    for (int k = 0; k < FC_PACK_COUNT; ++k) {
        const fc_bms_t *b = &bms[k];
        if (!b->valid || !b->cells_valid || now - b->rx_ms > FC_BMS_STALE_MS) {
            o->stop = FC_STOP_BMS_LOST; continue;
        }
        if (b->protection != 0 || !(b->fet & 0x01)) { o->stop = FC_STOP_BMS_FAULT; continue; }
        if (b->cell_max_mv >= FC_CELL_ABS_MAX_MV) { o->stop = FC_STOP_CELL_OV; continue; }
        if ((uint32_t)b->pack_mv_10 >= (uint32_t)FC_PACK_OVP_0P1V * 10u) { o->stop = FC_STOP_PACK_OV; continue; }

        int16_t tmax = -1000, tmin = 1000;
        for (uint8_t i = 0; i < b->ntc_count; ++i) { tmax = MAX(tmax, b->ntc_dc[i]); tmin = MIN(tmin, b->ntc_dc[i]); }
        if (b->ntc_count == 0) { o->stop = FC_STOP_TEMP; continue; }   /* no temperature = no fast charge */
        uint16_t der = MIN(fc_temp_derate_pct(tmax), fc_temp_derate_pct(tmin));
        if (der == 0) { o->stop = FC_STOP_TEMP; continue; }

        uint32_t lim = (uint32_t)FC_PACK_I_MAX_0P1A * der / 100u;
        lim = MIN(lim, (uint32_t)FC_BMS_RATED_CONT_0P1A);
        lim = MIN(lim, fc_cell_taper_0p1a(b->cell_max_mv, (uint16_t)lim));
        if (b->cell_min_mv < FC_CELL_MIN_CHARGE_MV) lim = MIN(lim, (uint32_t)FC_LOW_CELL_I_0P1A);
        o->lim_0p1a[k] = (uint16_t)lim;

        if (b->cell_max_mv > o->cell_max_mv) { o->cell_max_mv = b->cell_max_mv; o->cell_max_no = (uint8_t)(k * FC_CELLS_SERIES + b->cell_max_idx); }
        o->cell_min_mv = MIN(o->cell_min_mv, b->cell_min_mv);
        o->tmax_dc = MAX(o->tmax_dc, tmax); o->tmin_dc = MIN(o->tmin_dc, tmin);
        uint32_t cap = b->nominal_10mah ? b->nominal_10mah : 1u;
        cap_sum += cap; soc_w += cap * b->soc_pct;
        o->pack_0p1v = MAX(o->pack_0p1v, (uint16_t)(b->pack_mv_10 / 10u));
    }
    if (o->stop == FC_STOP_NONE && FC_PACK_COUNT == 2) {
        int32_t dv = (int32_t)bms[0].pack_mv_10 - (int32_t)bms[1].pack_mv_10;  /* 10 mV */
        if (dv < 0) dv = -dv;
        /* Only meaningful before the packs are paralleled; afterwards they are equal by definition. */
        if (dv > FC_PACK_IMBALANCE_0P1V * 10) o->stop = FC_STOP_PACK_IMBALANCE;
    }
    o->soc_pct = cap_sum ? (uint8_t)(soc_w / cap_sum) : 0;

    /* Current sharing: parallel packs do not split current equally. The total is limited so
       that the pack with the highest share hits exactly its own limit. */
    if (o->stop != FC_STOP_NONE) { o->total_lim_0p1a = 0; return; }
    int32_t i0 = bms[0].current_10ma > 0 ? bms[0].current_10ma : 0;
    int32_t i1 = bms[1].current_10ma > 0 ? bms[1].current_10ma : 0;
    uint32_t total = (uint32_t)o->lim_0p1a[0] + o->lim_0p1a[1];
    if (i0 + i1 > 500) {                         /* > 5 A: shares are meaningful */
        uint32_t t0 = (uint32_t)((int64_t)o->lim_0p1a[0] * (i0 + i1) / (i0 ? i0 : 1));
        uint32_t t1 = (uint32_t)((int64_t)o->lim_0p1a[1] * (i0 + i1) / (i1 ? i1 : 1));
        total = MIN(total, MIN(t0, t1));
    } else {
        total = 2u * MIN(o->lim_0p1a[0], o->lim_0p1a[1]);   /* assume equal split */
    }
    o->total_lim_0p1a = (uint16_t)MIN(total, (uint32_t)FC_TOTAL_I_MAX_0P1A);
}

bool fc_station_compatible(const fc_station_t *st)
{
    if (!st->cml_valid) return false;
    return st->cml_vmin_0p1v <= FC_STACK_VIN_MAX_0P1V && st->cml_vmax_0p1v >= FC_STACK_VIN_MIN_0P1V;
}

fc_hv_request_t fc_hv_request(const fc_station_t *st, uint16_t pout_w)
{
    fc_hv_request_t r = { .v_0p1v = FC_HV_REQ_V_0P1V, .i_0p1a = 0, .mode = FC_HV_BCL_MODE };
    uint16_t vlo = FC_STACK_VIN_MIN_0P1V, vhi = FC_STACK_VIN_MAX_0P1V;
    if (st->cml_valid) { vlo = MAX(vlo, st->cml_vmin_0p1v); vhi = MIN(vhi, st->cml_vmax_0p1v); }
    if (vlo > vhi) return r;                     /* incompatible, caller must stop */
    r.v_0p1v = MIN(MAX(r.v_0p1v, vlo), vhi);
    /* Pin = Pout / eff; I = Pin / V; + margin. 0.1 A units: I*10 = P*10/V. */
    uint32_t pin_w = (uint32_t)pout_w * 100u / FC_STACK_EFF_PCT;
    uint32_t i = pin_w * 100u / r.v_0p1v;                       /* 0.1 A (P*10 / (V/10) /10) */
    i = i * (100u + FC_HV_I_MARGIN_PCT) / 100u + FC_HV_I_MARGIN_MIN_0P1A;
    i = MIN(i, (uint32_t)FC_HV_I_REQ_MAX_0P1A);
    if (st->cml_valid) {
        i = MIN(i, (uint32_t)st->cml_imax_0p1a);
        i = MAX(i, (uint32_t)st->cml_imin_0p1a);
    }
    r.i_0p1a = (uint16_t)i;
    return r;
}

void fc_supervisor_init(fc_supervisor_t *s) { memset(s, 0, sizeof(*s)); }

void fc_supervisor_start(fc_supervisor_t *s, uint32_t now)
{
    memset(s, 0, sizeof(*s));
    s->running = true;
    s->last_ms = now;
}

void fc_supervisor_step(fc_supervisor_t *s, const fc_sup_in_t *in, uint32_t now, fc_sup_out_t *o)
{
    memset(o, 0, sizeof(*o));
    fc_limits_compute(in->bms, now, &o->lim);
    uint32_t dt = now - s->last_ms; s->last_ms = now;

    if (s->stop == FC_STOP_NONE && s->running) {
        if (in->user_stop) s->stop = FC_STOP_USER;
        else if (in->link_age_ms > FCL_STOP_MS) s->stop = FC_STOP_LINK_LOST;
        else if (in->station_end) s->stop = FC_STOP_STATION_END;
        else if (!in->cc2_ok) s->stop = FC_STOP_CC2_LOST;
        else if (in->power_path_ready && (!in->dcdc->valid || now - in->dcdc->rx_ms > FC_PSU_STALE_MS)) s->stop = FC_STOP_DCDC_LOST;
        else if (in->dcdc->fault || in->dcdc->temp_dc >= FC_PSU_T_TRIP_C * 10) s->stop = FC_STOP_DCDC_FAULT;
        else if (o->lim.stop != FC_STOP_NONE &&
                 !(o->lim.stop == FC_STOP_PACK_IMBALANCE && s->iref_0p1a > 0)) s->stop = o->lim.stop;
        else if (in->station->cml_valid && !fc_station_compatible(in->station)) s->stop = FC_STOP_STATION_RANGE;
    }
    if (s->stop != FC_STOP_NONE || !s->running) {
        s->iref_0p1a = 0;
        o->stop = s->stop;
        o->dcdc.enable = false;
        o->hv = fc_hv_request(in->station, 0);
        return;
    }

    /* Target current */
    uint32_t target = o->lim.total_lim_0p1a;
    target = MIN(target, (uint32_t)in->stack_i_max_0p1a);   /* PSU power limit (by caller) */
    if (in->dcdc->temp_dc >= FC_PSU_T_DERATE_C * 10) target /= 2u;
    if (!in->power_path_ready || in->link_age_ms > FCL_PAUSE_MS) target = 0;   /* pause */

    /* Slew: up limited, down immediate */
    uint32_t up = (uint32_t)FC_I_SLEW_0P1A_PER_S * dt / 1000u;
    if (target > s->iref_0p1a) s->iref_0p1a = (uint16_t)MIN(target, (uint32_t)s->iref_0p1a + MAX(up, 1u));
    else s->iref_0p1a = (uint16_t)target;

    /* Termination: pack at CV and measured current below FC_TERM_I for FC_TERM_HOLD_MS */
    int32_t itot = in->bms[0].current_10ma + in->bms[1].current_10ma;     /* 10 mA */
    bool at_cv = in->dcdc->vout_0p1v + 3u >= FC_PACK_CV_0P1V;
    if (in->power_path_ready && at_cv && itot < (int32_t)FC_TERM_I_0P1A * 10) {
        if (!s->term_since_ms) s->term_since_ms = now ? now : 1;
        else if (now - s->term_since_ms >= FC_TERM_HOLD_MS) s->stop = FC_STOP_FULL;
    } else {
        s->term_since_ms = 0;
    }
    if (s->stop != FC_STOP_NONE) {               /* latched this step: outputs off now */
        s->iref_0p1a = 0;
        o->stop = s->stop;
        o->hv = fc_hv_request(in->station, 0);
        return;
    }

    o->dcdc.enable = in->power_path_ready && s->stop == FC_STOP_NONE;
    o->dcdc.vref_0p1v = FC_PACK_CV_0P1V;
    o->dcdc.iref_0p1a = s->iref_0p1a;
    /* Ask the station for the power we plan to draw soon (target, not slewed value), so the
       station is never the limiting element. */
    uint32_t pout = (uint32_t)MAX(target, s->iref_0p1a) * FC_PACK_CV_0P1V / 100u;
    o->hv = fc_hv_request(in->station, (uint16_t)MIN(pout, 60000u));
    o->stop = s->stop;
}
