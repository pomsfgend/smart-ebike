#include "fc_link.h"
#include <string.h>

uint16_t fcl_crc16(const uint8_t *d, size_t n)
{
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; ++i) {
        c ^= (uint16_t)d[i] << 8;
        for (int b = 0; b < 8; ++b) c = (c & 0x8000u) ? (uint16_t)((c << 1) ^ 0x1021u) : (uint16_t)(c << 1);
    }
    return c;
}

typedef struct { uint8_t *p; size_t n; } wr_t;
static void w8(wr_t *w, uint8_t v) { w->p[w->n++] = v; }
static void w16(wr_t *w, uint16_t v) { w8(w, (uint8_t)v); w8(w, (uint8_t)(v >> 8)); }
static void w32(wr_t *w, uint32_t v) { w16(w, (uint16_t)v); w16(w, (uint16_t)(v >> 16)); }

typedef struct { const uint8_t *p; size_t n, i; } rd_t;
static uint8_t r8(rd_t *r) { return r->i < r->n ? r->p[r->i++] : 0; }
static uint16_t r16(rd_t *r) { uint16_t lo = r8(r); return (uint16_t)(lo | ((uint16_t)r8(r) << 8)); }
static uint32_t r32(rd_t *r) { uint32_t lo = r16(r); return lo | ((uint32_t)r16(r) << 16); }

/* frame: 'F' 'C' ver type len(payload) payload... crc16(LE, over everything before) */
static size_t finish(wr_t *w)
{
    w->p[4] = (uint8_t)(w->n - 5u);
    uint16_t c = fcl_crc16(w->p, w->n);
    w16(w, c);
    return w->n;
}

static void head(wr_t *w, fcl_type_t t) { w8(w, FCL_MAGIC0); w8(w, FCL_MAGIC1); w8(w, FCL_VERSION); w8(w, (uint8_t)t); w8(w, 0); }

size_t fcl_pack_station(uint8_t *out, const fcl_station_t *s)
{
    wr_t w = { out, 0 };
    head(&w, FCL_T_STATION);
    w16(&w, s->seq); w16(&w, s->ack);
    w8(&w, s->session); w8(&w, s->protocol); w8(&w, s->flags); w8(&w, s->cst_reason);
    w16(&w, s->cml_vmax); w16(&w, s->cml_vmin); w16(&w, s->cml_imax); w16(&w, s->cml_imin);
    w16(&w, s->ccs_v); w16(&w, s->ccs_i);
    w32(&w, s->uptime_ms);
    return finish(&w);
}

size_t fcl_pack_request(uint8_t *out, const fcl_request_t *r)
{
    wr_t w = { out, 0 };
    head(&w, FCL_T_REQUEST);
    w16(&w, r->seq); w16(&w, r->ack);
    w8(&w, r->flags); w8(&w, r->bcl_mode);
    w16(&w, r->bcl_v); w16(&w, r->bcl_i); w16(&w, r->bcs_v); w16(&w, r->bcs_i); w16(&w, r->bhm_vmax);
    w16(&w, r->cell_vmax_mv); w8(&w, r->cell_vmax_no);
    w8(&w, (uint8_t)r->tmax_c); w8(&w, (uint8_t)r->tmin_c); w8(&w, r->tmax_no); w8(&w, r->tmin_no);
    w8(&w, r->soc); w16(&w, r->remain_min); w8(&w, r->bsm_byte5);
    w8(&w, r->bst_reason); w16(&w, r->bst_fault); w8(&w, r->bst_error);
    w16(&w, r->batt_v); w16(&w, (uint16_t)r->batt_i);
    w16(&w, r->bcp_imax); w16(&w, r->bcp_cell_cv);
    return finish(&w);
}

fcl_err_t fcl_unpack(const uint8_t *in, size_t n, fcl_type_t *type, fcl_station_t *s, fcl_request_t *q)
{
    if (n < 7) return FCL_E_SHORT;
    if (in[0] != FCL_MAGIC0 || in[1] != FCL_MAGIC1) return FCL_E_MAGIC;
    if (in[2] != FCL_VERSION) return FCL_E_VERSION;
    size_t len = in[4];
    if (n < len + 7u) return FCL_E_SHORT;
    uint16_t crc = (uint16_t)(in[5 + len] | ((uint16_t)in[6 + len] << 8));
    if (fcl_crc16(in, 5 + len) != crc) return FCL_E_CRC;
    rd_t r = { in + 5, len, 0 };
    *type = (fcl_type_t)in[3];
    if (*type == FCL_T_STATION && s) {
        memset(s, 0, sizeof(*s));
        s->seq = r16(&r); s->ack = r16(&r);
        s->session = r8(&r); s->protocol = r8(&r); s->flags = r8(&r); s->cst_reason = r8(&r);
        s->cml_vmax = r16(&r); s->cml_vmin = r16(&r); s->cml_imax = r16(&r); s->cml_imin = r16(&r);
        s->ccs_v = r16(&r); s->ccs_i = r16(&r);
        s->uptime_ms = r32(&r);
        return FCL_OK;
    }
    if (*type == FCL_T_REQUEST && q) {
        memset(q, 0, sizeof(*q));
        q->seq = r16(&r); q->ack = r16(&r);
        q->flags = r8(&r); q->bcl_mode = r8(&r);
        q->bcl_v = r16(&r); q->bcl_i = r16(&r); q->bcs_v = r16(&r); q->bcs_i = r16(&r); q->bhm_vmax = r16(&r);
        q->cell_vmax_mv = r16(&r); q->cell_vmax_no = r8(&r);
        q->tmax_c = (int8_t)r8(&r); q->tmin_c = (int8_t)r8(&r); q->tmax_no = r8(&r); q->tmin_no = r8(&r);
        q->soc = r8(&r); q->remain_min = r16(&r); q->bsm_byte5 = r8(&r);
        q->bst_reason = r8(&r); q->bst_fault = r16(&r); q->bst_error = r8(&r);
        q->batt_v = r16(&r); q->batt_i = (int16_t)r16(&r);
        q->bcp_imax = r16(&r); q->bcp_cell_cv = r16(&r);
        return FCL_OK;
    }
    return FCL_E_TYPE;
}

bool fcl_monitor_accept(fcl_monitor_t *m, uint16_t seq, uint32_t now)
{
    if (m->ever) {
        int16_t d = (int16_t)(seq - m->last_seq);
        /* Old or duplicate frame. A peer reboot restarts seq at 0: accept a big backward jump
           only if the link has been silent for longer than FCL_STOP_MS. */
        if (d <= 0 && !(now - m->last_ms > FCL_STOP_MS)) { m->dup++; return false; }
    }
    m->ever = true; m->last_seq = seq; m->last_ms = now; m->ok++;
    return true;
}

uint32_t fcl_monitor_age(const fcl_monitor_t *m, uint32_t now)
{
    return m->ever ? now - m->last_ms : UINT32_MAX;
}
