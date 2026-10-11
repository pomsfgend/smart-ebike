#include <stdio.h>
#include <string.h>
#include "gbt_codec.h"
#include "gbt_tp_tx.h"
#include "jbd_bms.h"
#include "fc_supervisor.h"
#include "fc_config.h"
#include "fc_link.h"
#include "flatpack2.h"
#include "charger_core.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)


/* ---------- codec ---------- */
static void test_ids(void)
{
    CHECK(GBTC_ID_BHM == 0x182756F4u); CHECK(GBTC_ID_BRO == 0x100956F4u);
    CHECK(GBTC_ID_BCL == 0x181056F4u); CHECK(GBTC_ID_BSM == 0x181356F4u);
    CHECK(GBTC_ID_BST == 0x101956F4u); CHECK(GBTC_ID_BSD == 0x181C56F4u);
    CHECK(GBTC_ID_BEM == 0x081E56F4u); CHECK(GBTC_ID_TP_CM_BMS == 0x1CEC56F4u);
    CHECK(GBTC_ID_CHM == 0x1826F456u); CHECK(GBTC_ID_CRM == 0x1801F456u);
    CHECK(GBTC_ID_CTS == 0x1807F456u); CHECK(GBTC_ID_CML == 0x1808F456u);
    CHECK(GBTC_ID_CRO == 0x100AF456u); CHECK(GBTC_ID_CCS == 0x1812F456u);
    CHECK(GBTC_ID_CST == 0x101AF456u); CHECK(GBTC_ID_CSD == 0x181DF456u);
    CHECK(GBTC_ID_CEM == 0x081FF456u); CHECK(GBTC_ID_TP_CM_CHG == 0x1CECF456u);
}

static void test_codec(void)
{
    uint8_t d[64];
    CHECK(gbtc_i_to_raw(500) == 3500);          /* 50 A charge -> 4000-500 */
    CHECK(gbtc_raw_to_i_abs(3500) == 500);
    CHECK(gbtc_enc_bcl(d, 4000, 200, 0x01) == 5);
    CHECK(d[0] == 0xA0 && d[1] == 0x0F && d[2] == (3800 & 0xFF) && d[3] == (3800 >> 8) && d[4] == 1);
    gbtc_bsm_t bsm = { .charge_allowed = true, .tmax_c = 25, .tmin_c = 20 };
    CHECK(gbtc_enc_bsm(d, &bsm) == 7);
    CHECK(d[5] == 0x00 && d[6] == 0xD0);       /* same as spoof: byte7 0xD0 */
    CHECK(d[1] == 75 && d[3] == 70);
    gbtc_bcs_t bcs = { .v_0p1v = 4000, .i_abs_0p1a = 175, .cell_vmax_0p01v = 345, .cell_group = 1, .soc_pct = 55, .remain_min = 0 };
    CHECK(gbtc_enc_bcs(d, &bcs) == 9);
    CHECK((d[4] | (d[5] << 8)) == (345 | (1 << 12)));
    CHECK(d[7] == 1 && d[8] == 0);             /* remain 0 replaced by 1 */
    gbtc_brm_t brm = { .ver = {1, 1, 0}, .battery_type = 3, .rated_cap_0p1ah = 1000, .rated_v_0p1v = 4000 };
    memcpy(brm.vin, "ABCDEFGHJKLMNPRST", 17);
    CHECK(gbtc_enc_brm(d, &brm) == 49);
    CHECK(d[0] == 1 && d[3] == 3 && d[6] == 0xA0 && d[24] == 'A' && d[40] == 'T');
    uint8_t cml[8] = { 0x4C, 0x1D, 0xD0, 0x07, 0x00, 0x00, 0xA0, 0x0F }; /* 750 V, 200 V, 400 A, 0 A */
    gbtc_cml_t c;
    CHECK(gbtc_dec_cml(cml, 8, &c));
    CHECK(c.vmax == 7500 && c.vmin == 2000 && c.imax_abs == 4000 && c.imin_abs == 0);
    uint8_t ccs[8] = { 0xA0, 0x0F, 0x24, 0x0F, 0x05, 0x00, 0xFD, 0xFF };
    gbtc_ccs_t cc;
    CHECK(gbtc_dec_ccs(ccs, 8, &cc) && cc.v_0p1v == 4000 && cc.i_abs_0p1a == 124 && cc.permit == 1);
    CHECK(gbtc_dec_ccs(ccs, 6, &cc) && cc.permit == 1 && !cc.has_permit);   /* 2011 */
}

/* ---------- TP ---------- */
static uint32_t tx_id[32]; static uint8_t tx_d[32][8]; static int tx_n;
static int fake_send(uint32_t id, const uint8_t *d, uint8_t dlc) { (void)dlc; if (tx_n < 32) { tx_id[tx_n] = id; memcpy(tx_d[tx_n], d, 8); tx_n++; } return 0; }

static void test_tp(void)
{
    gbt_tp_tx_t t; uint8_t p[49];
    for (int i = 0; i < 49; ++i) p[i] = (uint8_t)i;
    tx_n = 0;
    gbt_tp_tx_init(&t, fake_send, false);
    CHECK(gbt_tp_tx_start(&t, 0x000200, p, 49, 0));
    CHECK(tx_n == 1 && tx_d[0][0] == 0x10 && tx_d[0][1] == 49 && tx_d[0][3] == 7 && tx_d[0][6] == 0x02);
    uint8_t cts[8] = { 0x11, 7, 1, 0xFF, 0xFF, 0x00, 0x02, 0x00 };
    gbt_tp_tx_on_rx(&t, GBTC_ID_TP_CM_CHG, cts, 8, 5);
    for (uint32_t ms = 10; ms < 200; ms += 10) gbt_tp_tx_poll(&t, ms);
    CHECK(tx_n == 8);
    CHECK(tx_d[1][0] == 1 && tx_d[1][1] == 0 && tx_d[7][0] == 7 && tx_d[7][1] == 42 && tx_d[7][7] == 48);
    CHECK(t.st == GBT_TP_WAIT_ACK);
    uint8_t ack[8] = { 0x13, 49, 0, 7, 0xFF, 0x00, 0x02, 0x00 };
    gbt_tp_tx_on_rx(&t, GBTC_ID_TP_CM_CHG, ack, 8, 300);
    CHECK(t.st == GBT_TP_DONE);
    /* timeout without CTS */
    gbt_tp_tx_init(&t, fake_send, false);
    gbt_tp_tx_start(&t, 0x000600, p, 13, 1000);
    gbt_tp_tx_poll(&t, 1000 + GBT_TP_T_CTS_MS + 1);
    CHECK(t.st == GBT_TP_FAILED);
    /* BAM */
    tx_n = 0;
    gbt_tp_tx_init(&t, fake_send, true);
    gbt_tp_tx_start(&t, 0x001100, p, 9, 0);
    for (uint32_t ms = 10; ms < 100; ms += 10) gbt_tp_tx_poll(&t, ms);
    CHECK(tx_n == 3 && tx_d[0][0] == 0x20 && t.st == GBT_TP_DONE);
}

/* ---------- JBD ---------- */
static size_t make_jbd(uint8_t *o, uint8_t cmd, const uint8_t *data, uint8_t len)
{
    o[0] = 0xDD; o[1] = cmd; o[2] = 0; o[3] = len;
    memcpy(&o[4], data, len);
    uint32_t sum = len; for (int i = 0; i < len; ++i) sum += data[i];
    uint16_t chk = (uint16_t)(0x10000u - sum);
    o[4 + len] = chk >> 8; o[5 + len] = chk & 0xFF; o[6 + len] = 0x77;
    return (size_t)len + 7;
}

static void test_jbd(void)
{
    uint8_t req[7];
    jbd_build_read(req, 0x03);
    CHECK(req[0] == 0xDD && req[1] == 0xA5 && req[2] == 0x03 && req[4] == 0xFF && req[5] == 0xFD && req[6] == 0x77);
    uint8_t basic[27] = {0};
    basic[0] = 0x17; basic[1] = 0x70;          /* 6000 x 10 mV = 60.00 V */
    basic[2] = 0x13; basic[3] = 0x88;          /* 5000 x 10 mA = 50 A    */
    basic[6] = 0x27; basic[7] = 0x10;          /* 100.00 Ah              */
    basic[19] = 42; basic[20] = 0x03; basic[21] = 18; basic[22] = 2;
    basic[23] = 0x0B; basic[24] = 0xA5;        /* 2981 -> 25.0 C */
    basic[25] = 0x0B; basic[26] = 0x9B;        /* 2971 -> 24.0 C */
    uint8_t f[64]; size_t n = make_jbd(f, 0x03, basic, sizeof(basic));
    fc_bms_t b; memset(&b, 0, sizeof(b));
    CHECK(jbd_parse(f, n - 1, &b, 0) == JBD_NEED_MORE);
    CHECK(jbd_parse(f, n, &b, 7) == JBD_OK);
    CHECK(b.pack_mv_10 == 6000 && b.current_10ma == 5000 && b.soc_pct == 42 && b.ntc_dc[0] == 250 && b.ntc_dc[1] == 240);
    f[5] ^= 1;
    CHECK(jbd_parse(f, n, &b, 7) == JBD_BAD_CHECKSUM);
    uint8_t cells[36];
    for (int i = 0; i < 18; ++i) { uint16_t v = (uint16_t)(3300 + i); cells[2 * i] = v >> 8; cells[2 * i + 1] = v & 0xFF; }
    n = make_jbd(f, 0x04, cells, 36);
    CHECK(jbd_parse(f, n, &b, 8) == JBD_OK);
    CHECK(b.cell_max_mv == 3317 && b.cell_max_idx == 18 && b.cell_min_mv == 3300 && b.cell_min_idx == 1);
}

/* ---------- supervisor ---------- */
static void mk_bms(fc_bms_t *b, uint16_t cell_mv, int16_t t_dc, int32_t i_10ma, uint32_t now)
{
    memset(b, 0, sizeof(*b));
    b->valid = b->cells_valid = true; b->rx_ms = now;
    b->pack_mv_10 = (uint16_t)(cell_mv * 18u / 10u);
    b->current_10ma = i_10ma; b->fet = 0x03; b->soc_pct = 50;
    b->nominal_10mah = 10000; b->remain_10mah = 5000;
    b->ntc_count = 2; b->ntc_dc[0] = t_dc; b->ntc_dc[1] = t_dc;
    b->cell_count = 18; b->cell_max_mv = cell_mv; b->cell_min_mv = (uint16_t)(cell_mv - 10);
    b->cell_max_idx = 3; b->cell_min_idx = 7;
}

static void test_limits(void)
{
    fc_bms_t b[2]; fc_limits_t l;
    mk_bms(&b[0], 3300, 250, 0, 0); mk_bms(&b[1], 3300, 250, 0, 0);
    fc_limits_compute(b, 100, &l);
    CHECK(l.stop == FC_STOP_NONE && l.lim_0p1a[0] == 500 && l.total_lim_0p1a == 1000);
    /* uneven sharing 60/40 at 80 A total: pack0 would see 50 A at total 83.3 A */
    mk_bms(&b[0], 3300, 250, 4800, 0); mk_bms(&b[1], 3300, 250, 3200, 0);
    fc_limits_compute(b, 100, &l);
    CHECK(l.total_lim_0p1a == 833);
    /* cold */
    mk_bms(&b[0], 3300, 50, 0, 0); mk_bms(&b[1], 3300, 250, 0, 0);
    fc_limits_compute(b, 100, &l);
    CHECK(l.lim_0p1a[0] == 100 && l.total_lim_0p1a == 200);
    mk_bms(&b[0], 3300, -10, 0, 0);
    fc_limits_compute(b, 100, &l);
    CHECK(l.stop == FC_STOP_TEMP);
    /* taper at 3.60 V: half way between 3.55 and 3.65 */
    mk_bms(&b[0], 3600, 250, 0, 0); mk_bms(&b[1], 3300, 250, 0, 0);
    fc_limits_compute(b, 100, &l);
    CHECK(l.lim_0p1a[0] == 250);
    /* stale */
    mk_bms(&b[0], 3300, 250, 0, 0); mk_bms(&b[1], 3300, 250, 0, 0);
    fc_limits_compute(b, FC_BMS_STALE_MS + 1, &l);
    CHECK(l.stop == FC_STOP_BMS_LOST);
    /* fault flag */
    b[1].protection = 0x0001; b[0].rx_ms = b[1].rx_ms = 0;
    fc_limits_compute(b, 10, &l);
    CHECK(l.stop == FC_STOP_BMS_FAULT);
    CHECK(fc_temp_derate_pct(560) == 0 && fc_temp_derate_pct(300) == 100);
}

static void test_hv_request(void)
{
    fc_station_t st = { .cml_valid = true, .cml_vmax_0p1v = 7500, .cml_vmin_0p1v = 2000, .cml_imax_0p1a = 2500, .cml_imin_0p1a = 0 };
    fc_hv_request_t r = fc_hv_request(&st, 2400);
    CHECK(r.v_0p1v == 2500);
    /* 2400/0.90 = 2667 W / 250 V = 10.7 A * 1.15 + 1 A = 13.3 A */
    CHECK(r.i_0p1a >= 128 && r.i_0p1a <= 138);
    r = fc_hv_request(&st, 6000);
    CHECK(r.i_0p1a == FC_HV_I_REQ_MAX_0P1A);
    st.cml_vmin_0p1v = 2600;                            /* station min 260 V: still OK */
    r = fc_hv_request(&st, 2400);
    CHECK(r.v_0p1v == 2600 && fc_station_compatible(&st));
    st.cml_vmin_0p1v = 3000;                            /* 300..750 V station: PSUs cannot */
    CHECK(!fc_station_compatible(&st));
    st.cml_vmin_0p1v = 2000; st.cml_imax_0p1a = 100;   /* weak station 10 A */
    r = fc_hv_request(&st, 2400);
    CHECK(r.i_0p1a == 100);
}

static void test_supervisor(void)
{
    fc_supervisor_t s; fc_sup_out_t o;
    fc_bms_t b[2]; mk_bms(&b[0], 3300, 250, 0, 0); mk_bms(&b[1], 3300, 250, 0, 0);
    fc_dcdc_tel_t d = { .valid = true, .vin_0p1v = 2500, .vout_0p1v = 600, .temp_dc = 400 };
    fc_station_t st = { .cml_valid = true, .cml_vmax_0p1v = 7500, .cml_vmin_0p1v = 2000, .cml_imax_0p1a = 2500 };
    fc_sup_in_t in = { .bms = b, .dcdc = &d, .station = &st, .cc2_ok = true, .power_path_ready = true,
                       .link_age_ms = 50, .stack_i_max_0p1a = 1000 };
    fc_supervisor_start(&s, 0);
    uint32_t t = 0;
#define TICK() do { t += 100; d.rx_ms = t; b[0].rx_ms = b[1].rx_ms = t; fc_supervisor_step(&s, &in, t, &o); } while (0)
    for (int i = 0; i < 20; ++i) TICK();         /* 2 s ramp at 10 A/s */
    CHECK(o.stop == FC_STOP_NONE && o.dcdc.enable && o.dcdc.iref_0p1a == 200 && o.dcdc.vref_0p1v == 657);
    for (int i = 0; i < 200; ++i) TICK();
    CHECK(o.dcdc.iref_0p1a == 1000);
    in.stack_i_max_0p1a = 373;                   /* stage 1: one Flatpack string */
    TICK();
    CHECK(o.dcdc.iref_0p1a == 373);
    in.link_age_ms = FCL_PAUSE_MS + 1;           /* link pause -> 0 A, no stop yet */
    TICK();
    CHECK(o.dcdc.iref_0p1a == 0 && o.stop == FC_STOP_NONE);
    in.link_age_ms = 50;
    /* termination: at CV, 1 A total for 60 s */
    d.vout_0p1v = 657;
    b[0].current_10ma = 50; b[1].current_10ma = 50;
    for (int i = 0; i < 610 && o.stop == FC_STOP_NONE; ++i) TICK();
    CHECK(o.stop == FC_STOP_FULL && !o.dcdc.enable && o.dcdc.iref_0p1a == 0);
    /* PSU fault stops immediately */
    fc_supervisor_start(&s, t);
    d.fault = true; TICK();
    CHECK(o.stop == FC_STOP_DCDC_FAULT && !o.dcdc.enable);
    d.fault = false;
    fc_supervisor_start(&s, t); in.cc2_ok = false; TICK();
    CHECK(o.stop == FC_STOP_CC2_LOST);
    in.cc2_ok = true;
    fc_supervisor_start(&s, t); in.link_age_ms = FCL_STOP_MS + 1; TICK();
    CHECK(o.stop == FC_STOP_LINK_LOST);
#undef TICK
}

/* ---------- link ---------- */
static void test_link(void)
{
    uint8_t buf[FCL_MAX_LEN];
    fcl_station_t s = { .seq = 7, .ack = 3, .session = FCL_SESS_CHARGING, .protocol = 1,
        .flags = FCL_SF_CAN_ALIVE | FCL_SF_CML_VALID | FCL_SF_CCS_PERMIT, .cml_vmax = 7500, .cml_vmin = 2000,
        .cml_imax = 2500, .ccs_v = 2510, .ccs_i = 98, .uptime_ms = 123456 };
    size_t n = fcl_pack_station(buf, &s);
    CHECK(n == 7 + 24 && n <= FCL_MAX_LEN);
    fcl_type_t ty; fcl_station_t s2; fcl_request_t r2;
    CHECK(fcl_unpack(buf, n, &ty, &s2, &r2) == FCL_OK && ty == FCL_T_STATION);
    CHECK(s2.seq == 7 && s2.cml_vmax == 7500 && s2.ccs_v == 2510 && s2.uptime_ms == 123456 && s2.flags == s.flags);
    buf[10] ^= 0x40;
    CHECK(fcl_unpack(buf, n, &ty, &s2, &r2) == FCL_E_CRC);
    CHECK(fcl_unpack(buf, 5, &ty, &s2, &r2) == FCL_E_SHORT);
    fcl_request_t r = { .seq = 9, .flags = FCL_RF_READY | FCL_RF_ALLOW, .bcl_mode = 1, .bcl_v = 2500, .bcl_i = 133,
        .bcs_v = 2510, .bcs_i = 120, .bhm_vmax = 2750, .cell_vmax_mv = 3412, .cell_vmax_no = 21, .tmax_c = 31,
        .tmin_c = -2, .soc = 64, .remain_min = 40, .bst_fault = 0x0400, .batt_v = 601, .batt_i = -15 };
    n = fcl_pack_request(buf, &r);
    CHECK(fcl_unpack(buf, n, &ty, &s2, &r2) == FCL_OK && ty == FCL_T_REQUEST);
    CHECK(r2.bcl_i == 133 && r2.tmin_c == -2 && r2.batt_i == -15 && r2.bst_fault == 0x0400 && r2.cell_vmax_no == 21);
    CHECK(fcl_crc16((const uint8_t *)"123456789", 9) == 0x29B1);   /* CCITT-FALSE check value */
    fcl_monitor_t m = {0};
    CHECK(fcl_monitor_age(&m, 10) == UINT32_MAX);
    CHECK(fcl_monitor_accept(&m, 5, 100));
    CHECK(!fcl_monitor_accept(&m, 5, 150));       /* duplicate */
    CHECK(!fcl_monitor_accept(&m, 4, 160));       /* replay */
    CHECK(fcl_monitor_accept(&m, 6, 200) && fcl_monitor_age(&m, 450) == 250);
    CHECK(fcl_monitor_accept(&m, 0, 200 + FCL_STOP_MS + 1));   /* peer rebooted after silence */
}

/* ---------- Flatpack2 ---------- */
static void test_fp2(void)
{
    fp2_frame_t f; fp2_t p; memset(&p, 0, sizeof(p));
    fp2_build_set(&f, 160, 5760, 5950);           /* 16 A, 57.60 V, OVP 59.50 V */
    const uint8_t exp[8] = { 0xA0, 0x00, 0x80, 0x16, 0x80, 0x16, 0x3E, 0x17 };
    CHECK(f.id == 0x05FF4004u && f.dlc == 8 && memcmp(f.d, exp, 8) == 0);
    fp2_build_set(&f, 999, 3000, 5950);           /* clamps */
    CHECK((f.d[0] | (f.d[1] << 8)) == FP2_I_MAX_DA && (f.d[2] | (f.d[3] << 8)) == FP2_V_MIN_CV);
    uint8_t hello[8] = { 0x1B, 0x14, 0x11, 0x71, 0x23, 0x54, 0x18, 0x00 };
    CHECK(fp2_on_rx(&p, 0x05001418u, hello, 8, 10) && p.seen && p.serial[0] == 0x14 && p.serial[5] == 0x18);
    fp2_build_login(&f, &p);
    CHECK(f.id == 0x05004804u && f.d[0] == 0x14 && f.d[5] == 0x18 && f.d[6] == 0 && f.d[7] == 0);
    uint8_t st[8] = { 25, 0x72, 0x01, 0xE4, 0x14, 0xFA, 0x00, 33 };   /* 37.0 A, 53.48 V, 250 V */
    CHECK(fp2_on_rx(&p, 0x05014004u, st, 8, 20));
    CHECK(p.status_valid && p.id == 1 && p.state == 0x04 && p.i_da == 370 && p.vout_cv == 5348 && p.vin_v == 250 && p.t_out_c == 33);
    fp2_build_default_v(&f, &p, 4350);
    CHECK(f.id == 0x05019C00u && f.dlc == 5 && f.d[0] == 0x29 && (f.d[3] | (f.d[4] << 8)) == 4350);
    CHECK(fp2_i_limit_da(5350) == 373 && fp2_i_limit_da(4350) == FP2_I_MAX_DA);
    CHECK(!fp2_on_rx(&p, 0x1812F456u, st, 8, 30));     /* GB/T frame is not ours */
}

/* ---------- charger core: full flow ---------- */
static void test_core(void)
{
    charger_core_t c; cc_out_t o; cc_in_t in; memset(&in, 0, sizeof(in));
    charger_core_init(&c);
    uint32_t t = 1000;
    mk_bms(&in.bms[0], 3300, 250, 0, t); mk_bms(&in.bms[1], 3300, 250, 0, t);   /* 59.4 V */
    in.fp_count = 1; in.cc2_ok = true; in.link_age_ms = 50;
    in.st.session = FCL_SESS_HANDSHAKE;
    in.st.flags = FCL_SF_CAN_ALIVE | FCL_SF_CML_VALID;
    in.st.cml_vmax = 7500; in.st.cml_vmin = 2000; in.st.cml_imax = 400; in.st.cml_imin = 0;
    fp2_t *fp = &in.fp[0];
#define STEP() do { t += 100; in.bms[0].rx_ms = in.bms[1].rx_ms = t; if (fp->status_valid) fp->status_ms = t; charger_core_step(&c, &in, t, &o); } while (0)
    STEP(); STEP();
    CHECK(c.state == CS_ARMED && (o.req.flags & FCL_RF_READY) && !o.hv_permit && !o.kbat && o.req.bcl_i == 0);
    CHECK(o.req.bhm_vmax == FC_HV_MAX_V_0P1V && o.req.bcl_v == 2500 && o.req.soc == 50);
    in.st.session = FCL_SESS_READY; STEP();
    CHECK(o.hv_permit && !o.kbat);
    in.st.session = FCL_SESS_CHARGING;
    in.st.flags |= FCL_SF_CCS_VALID | FCL_SF_CCS_PERMIT | FCL_SF_RELAY_B;
    STEP();
    CHECK(c.state == CS_WAIT_HV && !o.crps_on && o.req.bcl_i > 0 && (o.req.flags & FCL_RF_ALLOW));
    /* station HV reaches the PSUs: Flatpack boots at its default 43.5 V */
    fp->status_valid = true; fp->state = 0x04; fp->vin_v = 250; fp->vout_cv = 4350; fp->id = 1;
    in.vstack_0p1v = 435; STEP();
    CHECK(c.state == CS_WAIT_HV && o.crps_on);
    in.vstack_0p1v = 435 + 122; STEP();          /* CRPS running */
    CHECK(c.state == CS_MATCH);
    STEP();
    CHECK(!o.kbat && o.fp_v_cv == (594 - 122) * 10 && o.fp_i_da == FC_MATCH_I_0P1A);
    fp->vout_cv = 4720; in.vstack_0p1v = 594; STEP();
    CHECK(c.state == CS_CHARGING && o.kbat);
    for (int i = 0; i < 50; ++i) STEP();         /* 5 s ramp */
    CHECK(o.kbat && o.fp_enable && o.fp_v_cv == (657 - 122) * 10 && o.fp_i_da == FC_STRING_I_MAX_0P1A);   /* string limit 37 A */
    CHECK(o.req.bcl_i > 0 && o.req.bcl_i <= 400 && (o.req.flags & FCL_RF_ALLOW));
    /* link pause: current to 0 but contactor stays */
    in.link_age_ms = FCL_PAUSE_MS + 50; STEP();
    CHECK(c.state == CS_CHARGING && o.fp_i_da == 0 && o.kbat);
    /* link lost: ramp down, open when current ~0 */
    in.link_age_ms = FCL_STOP_MS + 1; STEP();
    CHECK(c.state == CS_RAMPDOWN && c.stop == FC_STOP_LINK_LOST && o.fp_i_da == 0 && o.kbat);
    STEP(); STEP(); STEP();
    CHECK(c.state == CS_DONE && !o.kbat && !o.crps_on && !o.hv_permit && (o.req.flags & FCL_RF_STOP));
    CHECK(o.req.bst_fault != 0);
    in.link_age_ms = 50; in.st.session = FCL_SESS_IDLE; STEP();
    CHECK(c.state == CS_IDLE);
    /* station over-voltage during charging -> station_range stop */
    in.st.session = FCL_SESS_HANDSHAKE; in.st.cml_vmin = 2000; STEP(); STEP();
    in.st.session = FCL_SESS_CHARGING; STEP();
    CHECK(c.state == CS_WAIT_HV);
    fp->vin_v = 290; STEP();
    CHECK(c.state == CS_RAMPDOWN && c.stop == FC_STOP_STATION_RANGE);
    STEP(); STEP(); STEP();
    CHECK(c.state == CS_DONE && !o.crps_on);
    fp->vin_v = 250; in.st.session = FCL_SESS_IDLE; STEP();
    CHECK(c.state == CS_IDLE);
    /* incompatible station (300 V min): never ready */
    in.st.session = FCL_SESS_HANDSHAKE; in.st.cml_vmin = 3000; STEP(); STEP();
    CHECK(c.state == CS_ARMED && !(o.req.flags & FCL_RF_READY));
    in.st.session = FCL_SESS_CHARGING; STEP();
    CHECK(c.state == CS_DONE && c.stop == FC_STOP_STATION_RANGE && !o.kbat);
#undef STEP
}

int main(void)
{
    test_ids(); test_codec(); test_tp(); test_jbd(); test_limits(); test_hv_request(); test_supervisor();
    test_link(); test_fp2(); test_core();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
