#include "direct_core.h"
#include "fc_config.h"
#include <string.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

const char *direct_state_name(ds_state_t s)
{
    static const char *n[] = { "idle", "armed", "insul", "close", "charging", "rampdown", "done" };
    return (unsigned)s < sizeof(n) / sizeof(n[0]) ? n[s] : "?";
}

void direct_core_init(direct_core_t *c) { memset(c, 0, sizeof(*c)); }

static void go(direct_core_t *c, ds_state_t s, uint32_t now) { c->state = s; c->state_ms = now; }

uint16_t direct_bcl_voltage(const fcl_station_t *st)
{
    uint16_t v = FC_PACK_CV_0P1V;                 /* 65.7 V                                  */
    if (st && (st->flags & FCL_SF_CML_VALID)) {
        if (st->cml_vmin > v) v = st->cml_vmin;   /* never ask below CML min (2023 11.3.1)  */
        if (st->cml_vmax && v > st->cml_vmax) return 0;
    }
    if (v > FC_D_BCP_VMAX_0P1V) return 0;         /* would exceed our own BCP/BHM max V     */
    return v;
}

int32_t direct_batt_current_0p1a(const fc_bms_t b[2])
{
    int32_t s = 0;
    for (int k = 0; k < FC_PACK_COUNT; ++k) if (b[k].valid) s += b[k].current_10ma / 10;   /* 10 mA -> 0.1 A */
    return s;
}

static uint16_t pack_v(const fc_bms_t b[2])
{
    uint16_t v = 0;
    for (int k = 0; k < FC_PACK_COUNT; ++k) if (b[k].valid) v = MAX(v, (uint16_t)(b[k].pack_mv_10 / 10u));
    return v;
}

static void stop(direct_core_t *c, fc_stop_reason_t r, bool hard, uint32_t now)
{
    if (c->stop == FC_STOP_NONE) c->stop = r;
    c->hard_stop = c->hard_stop || hard;
    if (c->state == DS_CHARGING || c->state == DS_CLOSE) go(c, DS_RAMPDOWN, now);
    else if (c->state != DS_RAMPDOWN) go(c, DS_DONE, now);
}

static void bst_fill(fcl_request_t *r, fc_stop_reason_t s)
{
    /* GB/T 27930-2015 BST: byte1 reason (2-bit fields), bytes2-3 fault, byte4 error */
    r->bst_reason = 0; r->bst_fault = 0; r->bst_error = 0;
    switch (s) {
    case FC_STOP_FULL:        r->bst_reason = 0x01; break;          /* SOC target reached  */
    case FC_STOP_PACK_OV:
    case FC_STOP_HW_OVP:      r->bst_reason = 0x04; break;          /* total V setpoint    */
    case FC_STOP_CELL_OV:     r->bst_reason = 0x10; break;          /* cell V setpoint     */
    case FC_STOP_TEMP:        r->bst_fault = 0x0100; break;         /* pack over-temp      */
    case FC_STOP_OVERCURRENT: r->bst_error = 0x01; break;           /* current too high    */
    case FC_STOP_INLET:
    case FC_STOP_STATION_RANGE: r->bst_error = 0x04; break;         /* voltage abnormal    */
    case FC_STOP_USER:        break;
    default:                  r->bst_fault = 0x4000; break;         /* other fault         */
    }
}

void direct_core_step(direct_core_t *c, const ds_in_t *in, uint32_t now, ds_out_t *o)
{
    memset(o, 0, sizeof(*o));
    uint32_t dt = c->last_ms ? now - c->last_ms : 0;
    c->last_ms = now;
    fc_limits_compute(in->bms, now, &o->lim);
    const fcl_station_t *st = in->st;
    uint8_t sess = st ? st->session : FCL_SESS_IDLE;
    bool link_ok = st && in->link_age_ms <= FCL_STOP_MS;
    uint16_t vpack = pack_v(in->bms);
    int32_t ibat = direct_batt_current_0p1a(in->bms);
    uint16_t bclv = direct_bcl_voltage(st);
    bool active = c->state >= DS_INSUL && c->state <= DS_CHARGING;

    /* ---- stop conditions (all states where the session is active) ---- */
    if (active) {
        if (in->hw_ovp) stop(c, FC_STOP_HW_OVP, true, now);
        else if (!link_ok) stop(c, FC_STOP_LINK_LOST, true, now);
        else if (o->lim.stop != FC_STOP_NONE && !(o->lim.stop == FC_STOP_PACK_IMBALANCE && c->state >= DS_CHARGING))
            stop(c, o->lim.stop, o->lim.stop == FC_STOP_CELL_OV || o->lim.stop == FC_STOP_PACK_OV, now);
        else if (in->user_stop) stop(c, FC_STOP_USER, false, now);
        else if (!in->cc2_ok) stop(c, FC_STOP_CC2_LOST, true, now);
        else if (sess == FCL_SESS_STOPPING || (st->flags & FCL_SF_STATION_END)) stop(c, FC_STOP_STATION_END, false, now);
        else if ((st->flags & FCL_SF_CML_VALID) && bclv == 0) stop(c, FC_STOP_STATION_RANGE, false, now);
    }

    switch (c->state) {
    case DS_IDLE:
    case DS_DONE:
        if (c->state == DS_DONE && sess != FCL_SESS_IDLE) break;   /* re-arm only after the session ended */
        if (c->state == DS_DONE) { direct_core_init(c); c->last_ms = now; }
        if (in->enable && link_ok && o->lim.stop == FC_STOP_NONE) go(c, DS_ARMED, now);
        break;
    case DS_ARMED:
        if (!in->enable || !link_ok) { go(c, DS_IDLE, now); break; }
        if (sess >= FCL_SESS_HANDSHAKE && sess <= FCL_SESS_PARAM) { go(c, DS_INSUL, now); c->inlet_ok_since = 0; }
        break;
    case DS_INSUL:
        /* K-BAT open: the station may put up to BHM max V on the inlet for its IMD test. Wait
           for parameter phase (CRM 0xAA seen by the cube) + inlet discharged <= 60 V. */
        if (sess == FCL_SESS_PARAM && (st->flags & FCL_SF_CML_VALID) && in->vinlet_0p1v <= FC_D_INLET_SAFE_0P1V) {
            if (!c->inlet_ok_since) c->inlet_ok_since = now ? now : 1;
            if (now - c->inlet_ok_since >= FC_D_INLET_SETTLE_MS) go(c, DS_CLOSE, now);
        } else c->inlet_ok_since = 0;
        break;
    case DS_CLOSE: {
        uint32_t t = now - c->state_ms;
        if (t >= FC_D_CLOSE_CHECK_MS) {
            int32_t dv = (int32_t)in->vinlet_0p1v - (int32_t)vpack;
            if (dv < 0) dv = -dv;
            if (dv > FC_D_CLOSE_DV_0P1V) { stop(c, FC_STOP_INLET, true, now); break; }
            if (sess == FCL_SESS_CHARGING) { go(c, DS_CHARGING, now); c->i_req_0p1a = FC_D_I_START_0P1A; c->term_since = c->ocp_since = c->overshoot_since = c->minI_since = 0; }
        }
        break; }
    case DS_CHARGING: {
        uint16_t umax = in->user_i_max_0p1a ? in->user_i_max_0p1a : FC_D_I_USER_MAX_0P1A;
        uint32_t lim = MIN((uint32_t)o->lim.total_lim_0p1a, (uint32_t)umax);
        if (st->cml_imax) lim = MIN(lim, (uint32_t)st->cml_imax);
        uint32_t i = c->i_req_0p1a;
        uint32_t up = (uint32_t)FC_I_SLEW_0P1A_PER_S * dt / 1000u;
        i = MIN(i + MAX(up, 1u), lim);
        /* our own CV: the station regulates current only, so pack voltage is ours to hold */
        if (vpack > FC_PACK_CV_0P1V) {
            uint32_t dec = (uint32_t)(vpack - FC_PACK_CV_0P1V) * FC_D_CV_KP_0P1A_PER_0P1V;
            i = i > dec ? i - dec : 0;
        } else if (vpack == FC_PACK_CV_0P1V) {
            i = MIN(i, (uint32_t)c->i_req_0p1a);   /* hold */
        }
        c->i_req_0p1a = (uint16_t)i;

        /* termination: CV reached and current below FC_TERM_I for FC_TERM_HOLD_MS */
        if (vpack >= FC_PACK_CV_0P1V - 2 && ibat < FC_TERM_I_0P1A) {
            if (!c->term_since) c->term_since = now ? now : 1;
            if (now - c->term_since >= FC_TERM_HOLD_MS) { stop(c, FC_STOP_FULL, false, now); break; }
        } else c->term_since = 0;
        /* the station cannot go below its CML min current: if we need less for long -> full */
        if (st->cml_imin && i < st->cml_imin && vpack >= FC_PACK_CV_0P1V - 2) {
            if (!c->minI_since) c->minI_since = now ? now : 1;
            if (now - c->minI_since >= FC_D_MIN_I_STOP_MS) { stop(c, FC_STOP_FULL, false, now); break; }
        } else c->minI_since = 0;
        /* battery-side OCP vs OUR limit (BMS measured, sum of packs) */
        int32_t ocp = (int32_t)lim + MAX((int32_t)FC_D_OCP_MARGIN_0P1A, (int32_t)lim * FC_D_OCP_PCT / 100);
        if (ibat > ocp) {
            if (!c->ocp_since) c->ocp_since = now ? now : 1;
            uint32_t t = now - c->ocp_since;
            if (t >= FC_D_OCP_HARD_MS) { stop(c, FC_STOP_OVERCURRENT, true, now); break; }
            if (t >= FC_D_OCP_SOFT_MS) c->i_req_0p1a = (uint16_t)(c->i_req_0p1a / 2);
        } else c->ocp_since = 0;
        /* station overshoot: CCS current above what we asked (2023 11.3.1: shall not exceed) */
        if ((st->flags & FCL_SF_CCS_VALID) &&
            (int32_t)st->ccs_i > (int32_t)c->i_req_0p1a + FC_D_STATION_OVERSHOOT_0P1A + c->i_req_0p1a / 10) {
            if (!c->overshoot_since) c->overshoot_since = now ? now : 1;
            if (now - c->overshoot_since >= FC_D_OCP_HARD_MS) { stop(c, FC_STOP_OVERCURRENT, true, now); break; }
        } else c->overshoot_since = 0;
        break; }
    case DS_RAMPDOWN: {
        uint32_t t = now - c->state_ms;
        int32_t ia = ibat < 0 ? -ibat : ibat;
        uint32_t tmo = c->hard_stop ? FC_D_RAMP_HARD_MS : FC_D_RAMP_SOFT_MS;
        if ((t >= FC_RAMPDOWN_MIN_MS && ia < FC_OPEN_I_0P1A) || t >= tmo) go(c, DS_DONE, now);
        break; }
    }

    /* ---- outputs ---- */
    o->kbat = (c->state == DS_CLOSE || c->state == DS_CHARGING || c->state == DS_RAMPDOWN) && !in->hw_ovp;
    o->hv_permit = c->state >= DS_ARMED && c->state <= DS_CHARGING && c->stop == FC_STOP_NONE;
    fcl_request_t *r = &o->req;
    r->flags = FCL_RF_DIRECT;
    if (c->state == DS_CHARGING || (c->state == DS_CLOSE && now - c->state_ms >= FC_D_CLOSE_CHECK_MS)) r->flags |= FCL_RF_READY;
    if (c->state == DS_CHARGING) r->flags |= FCL_RF_ALLOW;
    if (o->hv_permit) r->flags |= FCL_RF_HV_PERMIT;
    if (c->stop != FC_STOP_NONE) { r->flags |= FCL_RF_STOP; bst_fill(r, c->stop); }
    r->bcl_mode = FC_D_BCL_MODE;
    r->bcl_v = bclv ? bclv : FC_PACK_CV_0P1V;
    r->bcl_i = (c->state == DS_CHARGING) ? c->i_req_0p1a : 0;
    r->bhm_vmax = FC_D_BCP_VMAX_0P1V;
    r->bcp_imax = FC_D_BCP_IMAX_0P1A;
    r->bcp_cell_cv = FC_D_BCP_CELL_CV;
    r->batt_v = (c->state >= DS_CLOSE && c->state <= DS_RAMPDOWN && in->vinlet_0p1v) ? in->vinlet_0p1v : vpack;
    r->batt_i = (int16_t)MAX(MIN(ibat, 32767), -32768);
    r->bcs_v = r->batt_v;
    r->bcs_i = (uint16_t)(ibat > 0 ? ibat : 0);
    r->cell_vmax_mv = o->lim.cell_max_mv;
    r->cell_vmax_no = o->lim.cell_max_no;
    r->tmax_c = (int8_t)(o->lim.tmax_dc / 10); r->tmin_c = (int8_t)(o->lim.tmin_dc / 10);
    r->tmax_no = 1; r->tmin_no = 2;
    r->soc = o->lim.soc_pct;
    r->bsm_byte5 = 0x00;            /* abnormal BSM bits make the station CST; we stop via BST */
    if (c->state == DS_CHARGING && c->i_req_0p1a > 0 && ibat > 0) {
        uint32_t cap = 0;
        for (int k = 0; k < FC_PACK_COUNT; ++k) cap += in->bms[k].nominal_10mah;   /* 10 mAh */
        uint32_t need = cap * (100u - MIN(r->soc, 100u)) / 100u;                    /* 10 mAh */
        r->remain_min = (uint16_t)MIN(600u, need * 6u / (uint32_t)ibat);   /* 10 mAh / 0.1 A -> min */
    } else r->remain_min = 600;
}
