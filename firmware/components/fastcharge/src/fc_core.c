/* Pure session-glue logic: CAN decode -> station state, supervisor -> GB/T payloads. */
#include "fc_api.h"
#include "fc_config.h"
#include "fc_port.h"
#include "gbt_codec.h"
#include <string.h>

static const char *TAG = "FC";

static struct {
    fc_session_t session;
    fc_station_t station;
    fc_bms_t bms[2];
    fc_dcdc_tel_t dcdc;
    bool cc2_ok; uint16_t cc2_mv;
    bool batt_contactor;
    bool user_stop;
    bool cst_seen;
    fc_supervisor_t sup;
    fc_sup_out_t out;
} s;

void fc_core_init(void)
{
    fc_lock();
    memset(&s, 0, sizeof(s));
    fc_supervisor_init(&s.sup);
    fc_unlock();
}

void fc_on_can_rx(uint32_t id, const uint8_t *d, uint8_t dlc)
{
    /* Only class A charger frames (SA 0x56 -> PS 0xF4), priority ignored like the spoof does. */
    if (GBTC_SA(id) != GBTC_ADDR_CHARGER || GBTC_PS(id) != GBTC_ADDR_BMS) return;
    uint32_t now = fc_now_ms();
    fc_lock();
    switch (GBTC_PF(id)) {
    case GBTC_PF_CML: {
        gbtc_cml_t c;
        if (gbtc_dec_cml(d, dlc, &c)) {
            s.station.cml_valid = true;
            s.station.cml_vmax_0p1v = c.vmax; s.station.cml_vmin_0p1v = c.vmin;
            s.station.cml_imax_0p1a = c.imax_abs; s.station.cml_imin_0p1a = c.imin_abs;
        }
        break; }
    case GBTC_PF_CCS: {
        gbtc_ccs_t c;
        if (gbtc_dec_ccs(d, dlc, &c)) {
            s.station.ccs_valid = true; s.station.ccs_ms = now;
            s.station.ccs_v_0p1v = c.v_0p1v; s.station.ccs_i_0p1a = c.i_abs_0p1a;
            s.station.ccs_permit = c.permit == 0x01;
        }
        break; }
    case GBTC_PF_CST:
        s.cst_seen = true;
        break;
    default: break;
    }
    fc_unlock();
}

void fc_on_session_state(fc_session_t ss)
{
    uint32_t now = fc_now_ms();
    fc_lock();
    if (ss != s.session) {
        if (ss == FC_SESS_HANDSHAKE) {           /* new session: clear station data + latches */
            memset(&s.station, 0, sizeof(s.station));
            s.user_stop = false; s.cst_seen = false;
            fc_supervisor_start(&s.sup, now);
        }
        if (ss == FC_SESS_IDLE) s.sup.running = false;
        s.session = ss;
    }
    fc_unlock();
    FC_LOGI(TAG, "session -> %d", (int)ss);
}

void fc_user_stop(void) { fc_lock(); s.user_stop = true; fc_unlock(); }

void fc_set_bms(int i, const fc_bms_t *b) { if (i < 0 || i > 1) return; fc_lock(); s.bms[i] = *b; fc_unlock(); }
void fc_set_dcdc(const fc_dcdc_tel_t *t) { fc_lock(); s.dcdc = *t; fc_unlock(); }
void fc_set_cc2(bool ok, uint16_t mv) { fc_lock(); s.cc2_ok = ok; s.cc2_mv = mv; fc_unlock(); }
void fc_set_batt_contactor(bool c) { fc_lock(); s.batt_contactor = c; fc_unlock(); }

void fc_core_step(uint32_t now, fc_dcdc_cmd_t *cmd, bool *close_batt)
{
    fc_lock();
    bool charging = s.session == FC_SESS_CHARGING;
    bool ccs_fresh = s.station.ccs_valid && now - s.station.ccs_ms < 1000u;  /* CCS 50 ms, timeout 1 s */
    fc_sup_in_t in = {
        .bms = s.bms, .dcdc = &s.dcdc, .station = &s.station,
        .cc2_ok = s.cc2_ok, .user_stop = s.user_stop,
        .power_path_ready = charging && s.batt_contactor && ccs_fresh && s.station.ccs_permit &&
                            s.dcdc.vin_0p1v >= FC_DCDC_VIN_MIN_0P1V,
    };
    if (s.sup.running) fc_supervisor_step(&s.sup, &in, now, &s.out);
    else { memset(&s.out, 0, sizeof(s.out)); fc_limits_compute(s.bms, now, &s.out.lim); s.out.hv = fc_hv_request(&s.station, 0); }
    if (cmd) *cmd = s.out.dcdc;
    /* Battery-side contactor: closed from READY (needed for BRO=AA) until the session ends or a
       stop is latched. The DC-DC output precharge is done by the converter itself (see HARDWARE.md). */
    if (close_batt) *close_batt = (s.session == FC_SESS_READY || s.session == FC_SESS_CHARGING) &&
                                  s.out.stop == FC_STOP_NONE && s.out.lim.stop == FC_STOP_NONE;
    fc_unlock();
}

bool fc_ready_for_bro(void)
{
    fc_lock();
    bool ok = s.session == FC_SESS_READY && s.batt_contactor && s.cc2_ok &&
              s.out.lim.stop == FC_STOP_NONE && fc_station_compatible(&s.station) &&
              s.dcdc.valid && !s.dcdc.fault;
    fc_unlock();
    return ok;
}

bool fc_stop_requested(uint8_t *reason, uint16_t *fault, uint8_t *error)
{
    fc_lock();
    fc_stop_reason_t r = s.out.stop;
    fc_unlock();
    if (r == FC_STOP_NONE) return false;
    uint8_t rs = 0; uint16_t f = 0; uint8_t e = 0;
    switch (r) {
    case FC_STOP_FULL:      rs = GBTC_BST_VTOTAL_REACHED; break;
    case FC_STOP_CELL_OV:   rs = GBTC_BST_VCELL_REACHED; break;
    case FC_STOP_PACK_OV:   e = 0x04; break;                 /* voltage abnormal, VERIFY bits */
    case FC_STOP_TEMP:      f = GBTC_BSTF_BATT_OT; break;
    case FC_STOP_CC2_LOST:  f = GBTC_BSTF_CC2; break;
    case FC_STOP_USER:      rs = GBTC_BST_SOC_REACHED; break; /* "normal" end towards the station */
    default:                f = GBTC_BSTF_OTHER; break;
    }
    if (reason) *reason = rs;
    if (fault) *fault = f;
    if (error) *error = e;
    return true;
}

/* ---------------- payloads -------------------------------------------------------- */
/* The station sees a VIRTUAL HV battery whose voltage is the DC-DC input voltage. All V/I
   numbers below on the HV side are therefore station-side numbers, never the 65.7 V pack. */

size_t fc_fill_bhm(uint8_t *d) { return gbtc_enc_bhm(d, FC_HV_MAX_V_0P1V); }

size_t fc_fill_brm(uint8_t *d, bool proto_2011)
{
    gbtc_brm_t b = { .ver = { proto_2011 ? 0 : 1, 1, 0 }, .battery_type = 0x03 /* LFP, VERIFY */,
                     .rated_cap_0p1ah = FC_HV_CAPACITY_0P1AH, .rated_v_0p1v = FC_HV_RATED_V_0P1V };
    memcpy(b.vin, "00000000000000000", 17);   /* TODO(spoof): take app_state_vin() */
    size_t n = gbtc_enc_brm(d, &b);
    return proto_2011 ? 41 : n;
}

size_t fc_fill_bcp(uint8_t *d)
{
    fc_lock();
    fc_hv_request_t r = fc_hv_request(&s.station, FC_DCDC_POUT_MAX_W);
    gbtc_bcp_t b = {
        .cell_vmax_0p01v = FC_CELL_CV_MV / 10,    /* real cell limit, harmless for the station */
        .imax_0p1a = FC_HV_I_REQ_MAX_0P1A,
        .energy_0p1kwh = FC_HV_ENERGY_0P1KWH,
        .vmax_0p1v = FC_HV_MAX_V_0P1V,
        .tmax_c = 55,
        .soc_0p1pct = (uint16_t)s.out.lim.soc_pct * 10u,
        /* "current battery voltage": the station compares it with what it measures after
           K5/K6 close (DC-DC input capacitors). OPEN ISSUE, see PROTOCOL.md 6. */
        .vnow_0p1v = r.v_0p1v,
    };
    fc_unlock();
    return gbtc_enc_bcp(d, &b);
}

size_t fc_fill_bcl(uint8_t *d)
{
    fc_lock();
    fc_hv_request_t r = s.out.hv;
    if (!r.v_0p1v) r = fc_hv_request(&s.station, 0);
    fc_unlock();
    return gbtc_enc_bcl(d, r.v_0p1v, r.i_0p1a, r.mode);
}

size_t fc_fill_bcs(uint8_t *d)
{
    fc_lock();
    uint16_t cv = s.out.lim.cell_max_mv / 10u;
    uint32_t i_lv = (uint32_t)s.out.dcdc.iref_0p1a;
    uint32_t remain = 600;                     /* minutes, never 0 (spoof note) TODO: estimate */
    if (i_lv > 0 && s.bms[0].nominal_10mah) {
        uint32_t need_10mah = 0;
        for (int k = 0; k < 2; ++k) need_10mah += s.bms[k].nominal_10mah > s.bms[k].remain_10mah ?
                                                  (uint32_t)(s.bms[k].nominal_10mah - s.bms[k].remain_10mah) : 0u;
        remain = need_10mah * 60u / (i_lv * 10u) + 1u;   /* 10mAh / (0.1A*... ) -> min, rough */
    }
    gbtc_bcs_t b = {
        .v_0p1v = s.dcdc.vin_0p1v ? s.dcdc.vin_0p1v : s.station.ccs_v_0p1v,   /* HV side */
        .i_abs_0p1a = s.station.ccs_i_0p1a,  /* TODO: measure HV input current on the DC-DC */
        .cell_vmax_0p01v = cv, .cell_group = 1,
        .soc_pct = s.out.lim.soc_pct,
        .remain_min = (uint16_t)(remain > 600 ? 600 : remain),
    };
    fc_unlock();
    return gbtc_enc_bcs(d, &b);
}

size_t fc_fill_bsm(uint8_t *d)
{
    fc_lock();
    const fc_limits_t *l = &s.out.lim;
    gbtc_bsm_t b = {
        .cell_vmax_no = l->cell_max_no,
        .tmax_c = (int16_t)(l->tmax_dc / 10), .tmax_no = 1,
        .tmin_c = (int16_t)(l->tmin_dc / 10), .tmin_no = 1,
        .cell_v_state = l->stop == FC_STOP_CELL_OV ? 1 : 0,
        .overtemp = l->stop == FC_STOP_TEMP ? 1 : 0,
        /* Pause ("charge not allowed") while our side is not delivering, e.g. DC-DC fault.
           A pause is not an end for the spoof either (CCS permit 0 handling). */
        .charge_allowed = s.out.stop == FC_STOP_NONE && l->stop == FC_STOP_NONE,
    };
    if (b.tmax_c < -40 || b.tmax_c > 120) b.tmax_c = 25;   /* no data yet: neutral value */
    if (b.tmin_c < -40 || b.tmin_c > 120) b.tmin_c = 20;
    fc_unlock();
    return gbtc_enc_bsm(d, &b);
}

size_t fc_fill_bsd(uint8_t *d)
{
    fc_lock();
    gbtc_bsd_t b = { .soc_pct = s.out.lim.soc_pct,
                     .cell_vmin_0p01v = s.out.lim.cell_min_mv == 0xFFFF ? 0 : s.out.lim.cell_min_mv / 10u,
                     .cell_vmax_0p01v = s.out.lim.cell_max_mv / 10u,
                     .tmin_c = (int16_t)(s.out.lim.tmin_dc / 10), .tmax_c = (int16_t)(s.out.lim.tmax_dc / 10) };
    fc_unlock();
    return gbtc_enc_bsd(d, &b);
}

void fc_get_status(fc_status_t *o)
{
    fc_lock();
    o->session = s.session; o->station = s.station; o->out = s.out; o->dcdc = s.dcdc;
    o->cc2_ok = s.cc2_ok; o->cc2_mv = s.cc2_mv; o->batt_contactor = s.batt_contactor;
    fc_unlock();
}
