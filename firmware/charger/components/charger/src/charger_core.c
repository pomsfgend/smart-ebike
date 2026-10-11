#include "charger_core.h"
#include "fc_config.h"
#include <string.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

const char *cs_state_name(cs_state_t s)
{
    static const char *n[] = { "idle", "armed", "wait_hv", "match", "charging", "rampdown", "done" };
    return (unsigned)s < sizeof(n) / sizeof(n[0]) ? n[s] : "?";
}

void charger_core_init(charger_core_t *c)
{
    memset(c, 0, sizeof(*c));
    fc_supervisor_init(&c->sup);
}

static void go(charger_core_t *c, cs_state_t s, uint32_t now) { c->state = s; c->state_ms = now; }

static uint16_t batt_0p1v(const cc_in_t *in)
{
    uint16_t v = 0;
    for (int k = 0; k < 2; ++k) if (in->bms[k].valid) v = MAX(v, (uint16_t)(in->bms[k].pack_mv_10 / 10u));
    return v;
}

static int32_t batt_i_0p1a(const cc_in_t *in)
{
    return (in->bms[0].current_10ma + in->bms[1].current_10ma) / 10;
}

static void aggregate(charger_core_t *c, const cc_in_t *in, uint32_t now)
{
    fc_dcdc_tel_t *t = &c->tel;
    memset(t, 0, sizeof(*t));
    t->valid = in->fp_count > 0;
    t->rx_ms = now;
    uint16_t vfp_cv = 0;
    for (uint8_t k = 0; k < in->fp_count; ++k) {
        const fp2_t *p = &in->fp[k];
        if (!p->status_valid || now - p->status_ms > FC_PSU_STALE_MS) { t->valid = false; continue; }
        t->rx_ms = MIN(t->rx_ms, p->status_ms);
        t->vin_0p1v = (uint16_t)MAX(t->vin_0p1v, p->vin_v * 10u);
        t->iout_0p1a += (int16_t)p->i_da;
        t->temp_dc = (int16_t)MAX(t->temp_dc, p->t_out_c * 10);
        vfp_cv = MAX(vfp_cv, p->vout_cv);
        if (p->state == 0x0C) t->fault = true;
    }
    t->vout_0p1v = in->vstack_0p1v;
    uint16_t vfp = vfp_cv / 10u;
    c->vcrps_0p1v = in->vstack_0p1v > vfp ? (uint16_t)(in->vstack_0p1v - vfp) : 0;
}

static bool crps_running(const charger_core_t *c)
{
    return c->vcrps_0p1v >= FC_CRPS_V_MIN_0P1V && c->vcrps_0p1v <= FC_CRPS_V_MAX_0P1V;
}

/* Flatpack voltage for a wanted stack voltage */
static uint16_t fp_v_for(const charger_core_t *c, uint16_t vstack_0p1v)
{
    uint16_t vcrps = crps_running(c) ? c->vcrps_0p1v : FC_CRPS_V_NOM_0P1V;
    int32_t v = ((int32_t)vstack_0p1v - vcrps) * 10;
    if (v < (int32_t)FP2_V_MIN_CV) v = FP2_V_MIN_CV;
    if (v > (int32_t)FP2_V_MAX_CV) v = FP2_V_MAX_CV;
    return (uint16_t)v;
}

static void fill_request(const charger_core_t *c, const cc_in_t *in, cc_out_t *o, bool ready, bool allow, bool stop)
{
    fcl_request_t *r = &o->req;
    const fc_limits_t *l = &c->sup_out.lim;
    memset(r, 0, sizeof(*r));
    r->flags = (uint8_t)((ready ? FCL_RF_READY : 0) | (allow ? FCL_RF_ALLOW : 0) |
                         (stop ? FCL_RF_STOP : 0) | (o->hv_permit ? FCL_RF_HV_PERMIT : 0));
    r->bcl_mode = c->sup_out.hv.mode ? c->sup_out.hv.mode : FC_HV_BCL_MODE;
    r->bcl_v = c->sup_out.hv.v_0p1v ? c->sup_out.hv.v_0p1v : FC_HV_REQ_V_0P1V;
    r->bcl_i = (allow && !stop) ? c->sup_out.hv.i_0p1a : 0;
    r->bhm_vmax = FC_HV_MAX_V_0P1V;
    /* BCS "measured" HV values: real station voltage, and the input current we draw */
    uint16_t vin = c->tel.vin_0p1v ? c->tel.vin_0p1v : in->st.ccs_v;
    r->bcs_v = vin ? vin : FC_HV_REQ_V_0P1V;
    int32_t ib = batt_i_0p1a(in);
    uint16_t vb = batt_0p1v(in);
    if (ib > 0 && vin > 100) {
        uint32_t pin = (uint32_t)ib * vb / 10u * 100u / FC_STACK_EFF_PCT;    /* 0.1 W */
        r->bcs_i = (uint16_t)MIN(pin / vin * 10u / 10u, 4000u);
    }
    r->cell_vmax_mv = l->cell_max_mv;
    r->cell_vmax_no = l->cell_max_no;
    r->tmax_c = (int8_t)(l->tmax_dc / 10); r->tmin_c = (int8_t)(l->tmin_dc / 10);
    r->soc = l->soc_pct;
    if (ib > 0) {
        uint32_t cap = (uint32_t)in->bms[0].nominal_10mah + in->bms[1].nominal_10mah; /* 10 mAh */
        uint32_t need = cap * (100u - MIN(l->soc_pct, 100u)) / 100u;                /* 10 mAh */
        r->remain_min = (uint16_t)MIN(need * 6u / (uint32_t)ib / 10u * 10u, 600u);   /* rough */
    }
    r->bsm_byte5 = 0x00;                                   /* all "normal" (00) */
    r->batt_v = vb;
    r->batt_i = (int16_t)ib;
    /* BST byte 0: bit0-1 SOC target reached, 2-3 total V set, 4-5 cell V set; byte1-2 faults */
    switch (c->stop) {
    case FC_STOP_FULL:     r->bst_reason = 0x01; break;
    case FC_STOP_CELL_OV:  r->bst_reason = 0x10; break;
    case FC_STOP_PACK_OV:  r->bst_reason = 0x04; break;
    case FC_STOP_TEMP:     r->bst_fault = 0x0004; break;   /* battery over-temperature (VERIFY bit) */
    case FC_STOP_NONE:     break;
    default:               r->bst_fault = 0x0400; break;   /* "other fault" (VERIFY bit) */
    }
}

void charger_core_step(charger_core_t *c, const cc_in_t *in, uint32_t now, cc_out_t *o)
{
    memset(o, 0, sizeof(*o));
    aggregate(c, in, now);

    bool link_ok = in->link_age_ms <= FCL_STOP_MS;
    uint8_t sess = link_ok ? in->st.session : FCL_SESS_IDLE;
    bool station_end = (in->st.flags & FCL_SF_STATION_END) || sess == FCL_SESS_STOPPING;

    fc_station_t st = {0};
    st.cml_valid = (in->st.flags & FCL_SF_CML_VALID) != 0;
    st.cml_vmax_0p1v = in->st.cml_vmax; st.cml_vmin_0p1v = in->st.cml_vmin;
    st.cml_imax_0p1a = in->st.cml_imax; st.cml_imin_0p1a = in->st.cml_imin;
    st.ccs_valid = (in->st.flags & FCL_SF_CCS_VALID) != 0;
    st.ccs_v_0p1v = in->st.ccs_v; st.ccs_i_0p1a = in->st.ccs_i;
    st.ccs_permit = (in->st.flags & FCL_SF_CCS_PERMIT) != 0;

    uint16_t vb = batt_0p1v(in);
    uint16_t strings = in->fp_count ? in->fp_count : 1;

    /* power limit: per-string Flatpack current at the present Flatpack voltage */
    uint16_t vfp_cv = fp_v_for(c, FC_PACK_CV_0P1V);
    uint16_t istr = MIN(fp2_i_limit_da(vfp_cv), (uint16_t)FC_STRING_I_MAX_0P1A);

    fc_sup_in_t si = {
        .bms = in->bms, .dcdc = &c->tel, .station = &st,
        .cc2_ok = in->cc2_ok, .user_stop = in->user_stop,
        .station_end = station_end && c->state == CS_CHARGING,
        .link_age_ms = in->link_age_ms,
        .stack_i_max_0p1a = (uint16_t)(istr * strings),
        .power_path_ready = c->state == CS_CHARGING && crps_running(c) && st.ccs_permit,
    };
    if (c->state >= CS_WAIT_HV && c->state <= CS_CHARGING) {
        fc_supervisor_step(&c->sup, &si, now, &c->sup_out);
    } else {
        fc_sup_in_t idle = si; fc_supervisor_t tmp; fc_supervisor_init(&tmp);   /* limits only */
        idle.power_path_ready = false;
        fc_supervisor_step(&tmp, &idle, now, &c->sup_out);
    }

    bool lim_ok = c->sup_out.lim.stop == FC_STOP_NONE;
    bool compat = !st.cml_valid || fc_station_compatible(&st);
    uint32_t in_state = now - c->state_ms;

    /* any running state: latched stop or link lost -> ramp down */
    if (c->state >= CS_WAIT_HV && c->state <= CS_CHARGING) {
        fc_stop_reason_t r = c->sup.stop;
        if (r == FC_STOP_NONE && !link_ok) r = FC_STOP_LINK_LOST;
        if (r == FC_STOP_NONE && station_end) r = FC_STOP_STATION_END;
        /* station drives the PSUs outside their DC input rating (BHM/BCP said 275 V max) */
        if (r == FC_STOP_NONE && c->tel.valid && c->tel.vin_0p1v > FC_HV_MAX_V_0P1V) r = FC_STOP_STATION_RANGE;
        if (r == FC_STOP_NONE && c->state == CS_WAIT_HV && in_state > FC_WAIT_HV_TIMEOUT_MS) r = FC_STOP_DCDC_LOST;
        if (r == FC_STOP_NONE && c->state == CS_MATCH && in_state > FC_MATCH_TIMEOUT_MS) r = FC_STOP_PRECHARGE;
        if (r != FC_STOP_NONE) { c->stop = r; go(c, CS_RAMPDOWN, now); }
    }

    bool armed_ready = false;
    switch (c->state) {
    case CS_IDLE:
        if (sess >= FCL_SESS_HANDSHAKE && sess <= FCL_SESS_READY) { c->stop = FC_STOP_NONE; go(c, CS_ARMED, now); }
        break;
    case CS_ARMED: {
        armed_ready = lim_ok && compat && link_ok;
        o->hv_permit = armed_ready && sess >= FCL_SESS_READY;
        if (!link_ok || sess == FCL_SESS_IDLE) { go(c, CS_IDLE, now); break; }
        if (sess == FCL_SESS_CHARGING && armed_ready) { fc_supervisor_start(&c->sup, now); go(c, CS_WAIT_HV, now); }
        else if (sess == FCL_SESS_CHARGING) {
            c->stop = !lim_ok ? c->sup_out.lim.stop : (!compat ? FC_STOP_STATION_RANGE : FC_STOP_LINK_LOST);
            o->hv_permit = false;
            go(c, CS_DONE, now);
        }
        break;
    }
    case CS_WAIT_HV:
        o->hv_permit = true;
        if (c->tel.valid && c->tel.vin_0p1v >= FC_STACK_VIN_MIN_0P1V && c->tel.vin_0p1v <= FC_STACK_VIN_MAX_0P1V) {
            o->crps_on = true;
            if (crps_running(c)) go(c, CS_MATCH, now);
        }
        o->fp_enable = true; o->fp_v_cv = FP2_V_MIN_CV; o->fp_i_da = FC_MATCH_I_0P1A;
        break;
    case CS_MATCH: {
        o->hv_permit = true; o->crps_on = true; o->fp_enable = true;
        /* lowest reachable stack voltage = Flatpack min + CRPS; a deeply discharged pack may be
           below it -> close slightly above Vbat with the 2 A current limit active */
        uint16_t vmin = (uint16_t)(FP2_V_MIN_CV / 10u + c->vcrps_0p1v);
        uint16_t want = MAX(vb, vmin);
        o->fp_v_cv = fp_v_for(c, want);
        o->fp_i_da = FC_MATCH_I_0P1A;
        if (vb && in->vstack_0p1v >= vb && in->vstack_0p1v <= want + FC_MATCH_DV_0P1V && crps_running(c)) {
            o->kbat = true;
            go(c, CS_CHARGING, now);
        }
        break;
    }
    case CS_CHARGING:
        o->hv_permit = true; o->crps_on = true; o->fp_enable = true; o->kbat = true;
        o->fp_v_cv = fp_v_for(c, c->sup_out.dcdc.vref_0p1v ? c->sup_out.dcdc.vref_0p1v : FC_PACK_CV_0P1V);
        o->fp_i_da = (uint16_t)(c->sup_out.dcdc.iref_0p1a / strings);
        break;
    case CS_RAMPDOWN: {
        o->hv_permit = true; o->crps_on = true; o->fp_enable = true;
        o->fp_v_cv = FP2_V_MIN_CV; o->fp_i_da = 0;
        int32_t ib = batt_i_0p1a(in);
        bool low = ib < FC_OPEN_I_0P1A && c->tel.iout_0p1a < FC_OPEN_I_0P1A;
        uint32_t rd = now - c->state_ms;                 /* state may have changed this step */
        if (!((low && rd >= FC_RAMPDOWN_MIN_MS) || rd > FC_RAMPDOWN_TIMEOUT_MS)) o->kbat = true;     /* keep closed until I ~ 0 */
        else { o->hv_permit = false; o->crps_on = false; o->fp_enable = false; go(c, CS_DONE, now); }
        break;
    }
    case CS_DONE:
        if (sess == FCL_SESS_IDLE) go(c, CS_IDLE, now);
        break;
    }

    /* BSM "charge allowed" + non-zero BCL current from WAIT_HV on: the station must output
       voltage so that the PSUs can boot. Before CHARGING the supervisor target is 0, so the
       request is only the minimum margin (1 A + 15 %, at least CML Imin). */
    bool allow = c->state >= CS_WAIT_HV && c->state <= CS_CHARGING && c->sup.stop == FC_STOP_NONE;
    bool stop = c->state == CS_RAMPDOWN || c->state == CS_DONE;
    bool ready = c->state == CS_ARMED ? armed_ready : (c->state >= CS_WAIT_HV && !stop);
    fill_request(c, in, o, ready, allow, stop);
}
