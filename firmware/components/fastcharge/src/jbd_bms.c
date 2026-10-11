#include "jbd_bms.h"
#include <string.h>

static uint16_t be16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }

size_t jbd_build_read(uint8_t *o, uint8_t cmd)
{
    uint16_t chk = (uint16_t)(0x10000u - (uint32_t)cmd);
    o[0] = 0xDD; o[1] = 0xA5; o[2] = cmd; o[3] = 0x00;
    o[4] = (uint8_t)(chk >> 8); o[5] = (uint8_t)(chk & 0xFF); o[6] = 0x77;
    return 7;
}

jbd_result_t jbd_parse(const uint8_t *b, size_t n, fc_bms_t *m, uint32_t now)
{
    if (n < 7) return JBD_NEED_MORE;
    if (b[0] != 0xDD) return JBD_BAD_FRAME;
    uint8_t cmd = b[1], status = b[2], len = b[3];
    if (n < (size_t)len + 7u) return JBD_NEED_MORE;
    if (b[4 + len + 2] != 0x77) return JBD_BAD_FRAME;
    uint32_t sum = status + len;
    for (uint8_t i = 0; i < len; ++i) sum += b[4 + i];
    uint16_t chk = (uint16_t)(0x10000u - sum);
    if (be16(&b[4 + len]) != chk) return JBD_BAD_CHECKSUM;
    if (status != 0x00) return JBD_BMS_ERROR;
    const uint8_t *d = &b[4];

    if (cmd == JBD_CMD_BASIC) {
        if (len < 23) return JBD_BAD_FRAME;
        m->pack_mv_10    = be16(&d[0]);
        m->current_10ma  = (int16_t)be16(&d[2]);
        m->remain_10mah  = be16(&d[4]);
        m->nominal_10mah = be16(&d[6]);
        m->cycles        = be16(&d[8]);
        m->protection    = be16(&d[16]);
        m->soc_pct       = d[19];
        m->fet           = d[20];
        m->cell_count    = d[21];
        m->ntc_count     = d[22];
        if (m->ntc_count > 8) m->ntc_count = 8;
        for (uint8_t i = 0; i < m->ntc_count; ++i) {
            if (23u + 2u * i + 1u >= len) { m->ntc_count = i; break; }
            m->ntc_dc[i] = (int16_t)((int32_t)be16(&d[23 + 2 * i]) - 2731);
        }
        m->valid = true;
        m->rx_ms = now;
        return JBD_OK;
    }
    if (cmd == JBD_CMD_CELLS) {
        uint8_t cells = len / 2u;
        if (cells == 0 || cells > 32) return JBD_BAD_FRAME;
        m->cell_max_mv = 0; m->cell_min_mv = 0xFFFF;
        for (uint8_t i = 0; i < cells; ++i) {
            uint16_t v = be16(&d[2 * i]);
            m->cell_mv[i] = v;
            if (v > m->cell_max_mv) { m->cell_max_mv = v; m->cell_max_idx = (uint8_t)(i + 1); }
            if (v < m->cell_min_mv) { m->cell_min_mv = v; m->cell_min_idx = (uint8_t)(i + 1); }
        }
        m->cells_valid = true;
        m->rx_ms = now;
        return JBD_OK;
    }
    return JBD_BAD_FRAME;
}
