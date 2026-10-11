#include "gbt_tp_tx.h"
#include "gbt_codec.h"
#include <string.h>

static void cm_frame(uint8_t *f, uint8_t ctrl, const gbt_tp_tx_t *t)
{
    f[0] = ctrl;
    f[1] = (uint8_t)(t->len & 0xFF); f[2] = (uint8_t)(t->len >> 8);
    f[3] = t->packets;
    f[4] = 0xFF;
    f[5] = (uint8_t)(t->pgn & 0xFF); f[6] = (uint8_t)(t->pgn >> 8); f[7] = (uint8_t)(t->pgn >> 16);
}

static void send_dt(gbt_tp_tx_t *t, uint8_t seq)
{
    uint8_t f[8];
    memset(f, 0xFF, sizeof(f));
    f[0] = seq;
    uint16_t off = (uint16_t)(seq - 1u) * 7u;
    for (int i = 0; i < 7 && off + i < t->len; ++i) f[1 + i] = t->buf[off + i];
    t->send(GBTC_ID_TP_DT_BMS, f, 8);
}

void gbt_tp_tx_init(gbt_tp_tx_t *t, gbt_tp_send_fn send, bool use_bam)
{
    memset(t, 0, sizeof(*t));
    t->send = send;
    t->use_bam = use_bam;
}

bool gbt_tp_tx_start(gbt_tp_tx_t *t, uint32_t pgn, const uint8_t *p, uint16_t len, uint32_t now)
{
    if (gbt_tp_tx_busy(t) || len <= 8 || len > sizeof(t->buf)) return false;
    memcpy(t->buf, p, len);
    t->pgn = pgn; t->len = len; t->packets = (uint8_t)((len + 6u) / 7u);
    t->next_seq = 1; t->t_ms = now; t->t_last_dt_ms = 0;
    uint8_t f[8];
    if (t->use_bam) {
        cm_frame(f, 0x20, t);                 /* BAM, as spoof tx_bam() */
        t->last_allowed_seq = t->packets;
        t->st = GBT_TP_SENDING;
    } else {
        cm_frame(f, 0x10, t);                 /* RTS */
        f[4] = 0xFF;                          /* max packets per CTS: no limit */
        t->st = GBT_TP_WAIT_CTS;
    }
    t->send(GBTC_ID_TP_CM_BMS, f, 8);
    return true;
}

void gbt_tp_tx_on_rx(gbt_tp_tx_t *t, uint32_t id, const uint8_t *d, uint8_t dlc, uint32_t now)
{
    if (id != GBTC_ID_TP_CM_CHG || dlc < 8) return;
    uint32_t pgn = (uint32_t)d[5] | ((uint32_t)d[6] << 8) | ((uint32_t)d[7] << 16);
    if (pgn != t->pgn) return;
    switch (d[0]) {
    case 0x11: /* CTS */
        if (t->st != GBT_TP_WAIT_CTS && t->st != GBT_TP_SENDING) return;
        if (d[1] == 0) { t->st = GBT_TP_WAIT_CTS; t->t_ms = now; return; } /* hold */
        t->next_seq = d[2];
        t->last_allowed_seq = (uint8_t)(d[2] + d[1] - 1u);
        if (t->last_allowed_seq > t->packets) t->last_allowed_seq = t->packets;
        t->st = GBT_TP_SENDING; t->t_ms = now;
        break;
    case 0x13: /* EndOfMsgAck */
        t->st = GBT_TP_DONE; t->t_ms = now;
        break;
    case 0xFF: /* Abort */
        t->st = GBT_TP_FAILED; t->t_ms = now;
        break;
    default: break;
    }
}

void gbt_tp_tx_poll(gbt_tp_tx_t *t, uint32_t now)
{
    switch (t->st) {
    case GBT_TP_WAIT_CTS:
        if (now - t->t_ms > GBT_TP_T_CTS_MS) t->st = GBT_TP_FAILED;
        break;
    case GBT_TP_SENDING:
        if (t->t_last_dt_ms && now - t->t_last_dt_ms < GBT_TP_DT_GAP_MS) break;
        if (t->next_seq <= t->last_allowed_seq) {
            send_dt(t, t->next_seq++);
            t->t_last_dt_ms = now ? now : 1;
        }
        if (t->next_seq > t->packets) {
            t->st = t->use_bam ? GBT_TP_DONE : GBT_TP_WAIT_ACK;
            t->t_ms = now;
        } else if (t->next_seq > t->last_allowed_seq) {
            t->st = GBT_TP_WAIT_CTS; t->t_ms = now;
        }
        break;
    case GBT_TP_WAIT_ACK:
        if (now - t->t_ms > GBT_TP_T_ACK_MS) t->st = GBT_TP_FAILED;
        break;
    default: break;
    }
}

bool gbt_tp_tx_busy(const gbt_tp_tx_t *t)
{
    return t->st == GBT_TP_WAIT_CTS || t->st == GBT_TP_SENDING || t->st == GBT_TP_WAIT_ACK;
}
