#include "gbt_codec.h"
#include <string.h>

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v & 0xFFu); p[1] = (uint8_t)(v >> 8); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)get16(p) | ((uint32_t)get16(p + 2) << 16); }
static uint8_t temp_raw(int16_t c) { int v = c + 50; return (uint8_t)(v < 0 ? 0 : v > 250 ? 250 : v); }

uint16_t gbtc_i_to_raw(uint16_t i_abs)
{
    if (i_abs > 4000u) i_abs = 4000u;
    return (uint16_t)(4000u - i_abs);
}

uint16_t gbtc_raw_to_i_abs(uint16_t raw)
{
    return raw >= 4000u ? (uint16_t)(raw - 4000u) : (uint16_t)(4000u - raw);
}

size_t gbtc_enc_bhm(uint8_t *d, uint16_t vmax) { put16(d, vmax); return 2; }

size_t gbtc_enc_brm(uint8_t *d, const gbtc_brm_t *b)
{
    /* 0..2 version, 3 type, 4..5 cap, 6..7 voltage, 8..11 manufacturer, 12..15 pack no,
       16..18 date (year-1985, month, day), 19..21 cycle count, 22 ownership, 23 reserved,
       24..40 VIN, 41..48 BMS software version. VERIFY offsets 8..23 / 41..48. */
    memset(d, 0, GBTC_LEN_BRM);
    memcpy(d, b->ver, 3);
    d[3] = b->battery_type;
    put16(&d[4], b->rated_cap_0p1ah);
    put16(&d[6], b->rated_v_0p1v);
    d[23] = 0xFF;
    for (int i = 0; i < 17; ++i) d[24 + i] = b->vin[i] ? (uint8_t)b->vin[i] : (uint8_t)'0';
    memset(&d[41], 0xFF, 8);
    return GBTC_LEN_BRM;
}

size_t gbtc_enc_bcp(uint8_t *d, const gbtc_bcp_t *b)
{
    put16(&d[0], b->cell_vmax_0p01v);
    put16(&d[2], gbtc_i_to_raw(b->imax_0p1a));
    put16(&d[4], b->energy_0p1kwh);
    put16(&d[6], b->vmax_0p1v);
    d[8] = temp_raw(b->tmax_c);
    put16(&d[9], b->soc_0p1pct);
    put16(&d[11], b->vnow_0p1v);
    return GBTC_LEN_BCP;
}

size_t gbtc_enc_bro(uint8_t *d, bool ready) { d[0] = ready ? 0xAA : 0x00; return 1; }

size_t gbtc_enc_bcl(uint8_t *d, uint16_t v, uint16_t i_abs, uint8_t mode)
{
    put16(&d[0], v);
    put16(&d[2], gbtc_i_to_raw(i_abs));
    d[4] = mode;
    return 5;
}

size_t gbtc_enc_bcs(uint8_t *d, const gbtc_bcs_t *b)
{
    put16(&d[0], b->v_0p1v);
    put16(&d[2], gbtc_i_to_raw(b->i_abs_0p1a));
    put16(&d[4], (uint16_t)((b->cell_vmax_0p01v & 0x0FFFu) | ((uint16_t)(b->cell_group & 0x0Fu) << 12)));
    d[6] = b->soc_pct > 100 ? 100 : b->soc_pct;
    put16(&d[7], b->remain_min ? b->remain_min : 1u);
    return GBTC_LEN_BCS;
}

size_t gbtc_enc_bsm(uint8_t *d, const gbtc_bsm_t *b)
{
    d[0] = b->cell_vmax_no ? b->cell_vmax_no : 1;
    d[1] = temp_raw(b->tmax_c); d[2] = b->tmax_no ? b->tmax_no : 1;
    d[3] = temp_raw(b->tmin_c); d[4] = b->tmin_no ? b->tmin_no : 1;
    d[5] = (uint8_t)((b->cell_v_state & 3u) | ((b->soc_state & 3u) << 2) |
                     ((b->overcurrent & 3u) << 4) | ((b->overtemp & 3u) << 6));
    /* bits 1-2 insulation, 3-4 connector, 5-6 charge allowed (01), 7-8 reserved = 11.
       Same as spoof tx_bsm(): normal case -> 0xD0. */
    d[6] = (uint8_t)((b->insulation & 3u) | ((b->connector & 3u) << 2) |
                     ((b->charge_allowed ? 1u : 0u) << 4) | 0xC0u);
    return 7;
}

size_t gbtc_enc_bst(uint8_t *d, uint8_t reason, uint16_t fault, uint8_t error)
{
    d[0] = reason; put16(&d[1], fault); d[3] = error; return 4;
}

size_t gbtc_enc_bsd(uint8_t *d, const gbtc_bsd_t *b)
{
    d[0] = b->soc_pct;
    put16(&d[1], b->cell_vmin_0p01v);
    put16(&d[3], b->cell_vmax_0p01v);
    d[5] = temp_raw(b->tmin_c);
    d[6] = temp_raw(b->tmax_c);
    return 7;
}

bool gbtc_dec_chm(const uint8_t *d, uint8_t len, gbtc_chm_t *o)
{
    if (len < 3) return false;
    memcpy(o->ver, d, 3);
    return true;
}

bool gbtc_dec_crm(const uint8_t *d, uint8_t len, gbtc_crm_t *o)
{
    if (len < 1) return false;
    o->result = d[0];
    o->charger_no = len >= 5 ? get32(&d[1]) : 0;
    return true;
}

bool gbtc_dec_cml(const uint8_t *d, uint8_t len, gbtc_cml_t *o)
{
    if (len < 6) return false;               /* 2011: 6 bytes, 2015: 8 bytes */
    o->vmax = get16(&d[0]);
    o->vmin = get16(&d[2]);
    o->imax_abs = gbtc_raw_to_i_abs(get16(&d[4]));
    o->has_imin = len >= 8;
    o->imin_abs = o->has_imin ? gbtc_raw_to_i_abs(get16(&d[6])) : 0;
    return true;
}

bool gbtc_dec_cro(const uint8_t *d, uint8_t len, bool *ready)
{
    if (len < 1) return false;
    *ready = d[0] == 0xAA;
    return true;
}

bool gbtc_dec_ccs(const uint8_t *d, uint8_t len, gbtc_ccs_t *o)
{
    if (len < 6) return false;               /* 2011: 6 bytes, no permit */
    o->v_0p1v = get16(&d[0]);
    o->i_abs_0p1a = gbtc_raw_to_i_abs(get16(&d[2]));
    o->time_min = get16(&d[4]);
    o->has_permit = len >= 7;
    o->permit = o->has_permit ? (uint8_t)(d[6] & 0x03u) : 0x01u; /* 01 = allowed, 00 = pause */
    return true;
}

bool gbtc_dec_cst(const uint8_t *d, uint8_t len, gbtc_cst_t *o)
{
    if (len < 4) return false;
    o->reason = d[0]; o->fault = get16(&d[1]); o->error = d[3]; return true;
}

bool gbtc_dec_csd(const uint8_t *d, uint8_t len, gbtc_csd_t *o)
{
    if (len < 4) return false;
    o->time_min = get16(&d[0]); o->energy_0p1kwh = get16(&d[2]);
    o->charger_no = len >= 8 ? get32(&d[4]) : 0;
    return true;
}
