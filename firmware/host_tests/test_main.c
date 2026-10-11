#include <stdio.h>
#include <string.h>
#include "gbt_codec.h"
#include "gbt_tp_tx.h"
#include "jbd_bms.h"
#include "fc_supervisor.h"
#include "fc_api.h"
#include "fc_config.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint32_t g_now;
uint32_t fc_now_ms(void) { return g_now; }

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
    fc_hv_request_t r = fc_hv_request(&st, 6600);
    CHECK(r.v_0p1v == 4000);
    /* 6600/0.92 = 7173 W / 400 V = 17.9 A * 1.15 + 1 A = 21.6 A */
    CHECK(r.i_0p1a >= 210 && r.i_0p1a <= 220);
    st.cml_vmin_0p1v = 5000; st.cml_vmax_0p1v = 9500;  /* 500..950 V station */
    r = fc_hv_request(&st, 6600);
    CHECK(r.v_0p1v == 5000 && fc_station_compatible(&st));
    st.cml_vmin_0p1v = 5100;
    CHECK(!fc_station_compatible(&st));
    st.cml_vmin_0p1v = 2000; st.cml_imax_0p1a = 100;   /* weak station 10 A */
    r = fc_hv_request(&st, 6600);
    CHECK(r.i_0p1a == 100);
}

static void test_supervisor(void)
{
    fc_supervisor_t s; fc_sup_out_t o;
    fc_bms_t b[2]; mk_bms(&b[0], 3300, 250, 0, 0); mk_bms(&b[1], 3300, 250, 0, 0);
    fc_dcdc_tel_t d = { .valid = true, .vin_0p1v = 4000, .vout_0p1v = 600, .temp_dc = 400 };
    fc_station_t st = { .cml_valid = true, .cml_vmax_0p1v = 7500, .cml_vmin_0p1v = 2000, .cml_imax_0p1a = 2500 };
    fc_sup_in_t in = { .bms = b, .dcdc = &d, .station = &st, .cc2_ok = true, .power_path_ready = true };
    fc_supervisor_start(&s, 0);
    uint32_t t = 0;
    for (int i = 0; i < 20; ++i) {               /* 2 s ramp at 10 A/s */
        t += 100; d.rx_ms = t; b[0].rx_ms = b[1].rx_ms = t;
        fc_supervisor_step(&s, &in, t, &o);
    }
    CHECK(o.stop == FC_STOP_NONE && o.dcdc.enable && o.dcdc.iref_0p1a == 200 && o.dcdc.vref_0p1v == 657);
    for (int i = 0; i < 200; ++i) { t += 100; d.rx_ms = t; b[0].rx_ms = b[1].rx_ms = t; fc_supervisor_step(&s, &in, t, &o); }
    CHECK(o.dcdc.iref_0p1a == 1000);             /* 100 A, power 60 V*100 A = 6 kW < 6.6 kW */
    d.vout_0p1v = 657;                           /* power limit: 6600/65.7 = 100.4 A */
    t += 100; d.rx_ms = t; b[0].rx_ms = b[1].rx_ms = t; fc_supervisor_step(&s, &in, t, &o);
    CHECK(o.dcdc.iref_0p1a == 1000);
    /* termination: at CV, 1 A total for 60 s */
    b[0].current_10ma = 50; b[1].current_10ma = 50;
    for (int i = 0; i < 610 && o.stop == FC_STOP_NONE; ++i) { t += 100; d.rx_ms = t; b[0].rx_ms = b[1].rx_ms = t; fc_supervisor_step(&s, &in, t, &o); }
    CHECK(o.stop == FC_STOP_FULL && !o.dcdc.enable && o.dcdc.iref_0p1a == 0);
    /* DC-DC fault stops immediately */
    fc_supervisor_start(&s, t);
    d.fault = true; t += 100; d.rx_ms = t; b[0].rx_ms = b[1].rx_ms = t;
    fc_supervisor_step(&s, &in, t, &o);
    CHECK(o.stop == FC_STOP_DCDC_FAULT && !o.dcdc.enable);
    d.fault = false;
    /* CC2 lost */
    fc_supervisor_start(&s, t); in.cc2_ok = false; t += 100; d.rx_ms = t; b[0].rx_ms = b[1].rx_ms = t;
    fc_supervisor_step(&s, &in, t, &o);
    CHECK(o.stop == FC_STOP_CC2_LOST);
}

/* ---------- core / hooks: full flow ---------- */
static void test_core(void)
{
    fc_core_init();
    g_now = 1000;
    fc_on_session_state(FC_SESS_HANDSHAKE);
    uint8_t cml[8] = { 0x4C, 0x1D, 0xD0, 0x07, 0x24, 0x0E, 0xA0, 0x0F };  /* 750 V/200 V/-40 A..0 A */
    fc_on_can_rx(GBTC_ID_CML, cml, 8);
    fc_bms_t b[2]; mk_bms(&b[0], 3300, 250, 0, g_now); mk_bms(&b[1], 3300, 250, 0, g_now);
    fc_set_bms(0, &b[0]); fc_set_bms(1, &b[1]);
    fc_dcdc_tel_t d = { .valid = true, .rx_ms = g_now, .vin_0p1v = 0, .vout_0p1v = 594, .temp_dc = 300 };
    fc_set_dcdc(&d); fc_set_cc2(true, 6000);
    fc_on_session_state(FC_SESS_PARAM);
    uint8_t buf[64];
    CHECK(fc_fill_bcp(buf) == 13);
    CHECK((buf[11] | (buf[12] << 8)) == 4000);   /* virtual HV battery voltage */
    CHECK((buf[2] | (buf[3] << 8)) == 4000 - FC_HV_I_REQ_MAX_0P1A);
    fc_on_session_state(FC_SESS_READY);
    fc_dcdc_cmd_t cmd; bool k = false;
    fc_core_step(g_now, &cmd, &k);
    CHECK(k && !cmd.enable);                     /* close battery contactor, DC-DC still off */
    CHECK(!fc_ready_for_bro());                  /* contactor feedback not yet */
    fc_set_batt_contactor(true);
    CHECK(fc_ready_for_bro());
    fc_on_session_state(FC_SESS_CHARGING);
    uint8_t ccs[8] = { 0xA0, 0x0F, 0xA0, 0x0F, 0, 0, 0x01, 0xFF };
    d.vin_0p1v = 4000;
    for (int i = 0; i < 50; ++i) {
        g_now += 100; d.rx_ms = g_now; b[0].rx_ms = b[1].rx_ms = g_now;
        fc_set_dcdc(&d); fc_set_bms(0, &b[0]); fc_set_bms(1, &b[1]);
        fc_on_can_rx(GBTC_ID_CCS, ccs, 8);
        fc_core_step(g_now, &cmd, &k);
    }
    CHECK(cmd.enable && cmd.iref_0p1a == 500);   /* 5 s at 10 A/s */
    CHECK(fc_fill_bcl(buf) == 5);
    uint16_t v = buf[0] | (buf[1] << 8), iraw = buf[2] | (buf[3] << 8);
    CHECK(v == 4000);
    CHECK(4000 - iraw <= 400 && 4000 - iraw >= 50);   /* clamped by CML 40 A */
    CHECK(fc_fill_bsm(buf) == 7 && buf[6] == 0xD0);
    CHECK(fc_fill_bcs(buf) == 9 && buf[6] == 50);
    uint8_t r; uint16_t f; uint8_t e;
    CHECK(!fc_stop_requested(&r, &f, &e));
    fc_user_stop();
    g_now += 100; fc_core_step(g_now, &cmd, &k);
    CHECK(fc_stop_requested(&r, &f, &e) && !cmd.enable && !k);
    CHECK(fc_fill_bsd(buf) == 7);
}

int main(void)
{
    test_ids(); test_codec(); test_tp(); test_jbd(); test_limits(); test_hv_request(); test_supervisor(); test_core();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
