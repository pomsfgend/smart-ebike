#pragma once
/*
 * Multi-packet sender for BRM (49 B), BCP (13 B), BCS (9 B).
 *
 * GB/T 27930 uses the SAE J1939-21 transport with connection management:
 *   BMS  -> TP.CM_RTS  (0x10, size, packets, 0xFF, PGN[3])
 *   CHG  -> TP.CM_CTS  (0x11, packets_allowed, next_seq, 0xFF, 0xFF, PGN[3])
 *   BMS  -> TP.DT      (seq, 7 data bytes, pad 0xFF)
 *   CHG  -> TP.CM_EndOfMsgAck (0x13, size, packets, 0xFF, PGN[3])
 *   abort: 0xFF
 * The spoof (main/sm_relay.c tx_bam()) sends a BAM-style announce (0x20) without waiting for CTS,
 * and reports that this works on the stations it was tried on. The original factory dump contains
 * 0x11/0x13 handling (docs/CAN_MAP.md of the spoof), i.e. the factory firmware probably used RTS/CTS.
 * This module implements RTS/CTS and falls back to BAM when configured. VERIFY on real stations.
 *
 * Pure C: the caller provides send() and calls gbt_tp_tx_poll() every <=10 ms.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int (*gbt_tp_send_fn)(uint32_t id, const uint8_t *data, uint8_t dlc);

typedef enum { GBT_TP_IDLE, GBT_TP_WAIT_CTS, GBT_TP_SENDING, GBT_TP_WAIT_ACK, GBT_TP_DONE, GBT_TP_FAILED } gbt_tp_state_t;

typedef struct {
    gbt_tp_send_fn send;
    bool     use_bam;              /* true = spoof-compatible BAM, false = RTS/CTS */
    uint32_t pgn;
    uint8_t  buf[64];
    uint16_t len;
    uint8_t  packets;
    uint8_t  next_seq;             /* next TP.DT sequence to send (1-based) */
    uint8_t  last_allowed_seq;     /* last seq allowed by the current CTS    */
    uint32_t t_ms;                 /* timestamp of last state change         */
    uint32_t t_last_dt_ms;
    gbt_tp_state_t st;
} gbt_tp_tx_t;

/* J1939-21 timeouts T1..T4 are 750/1250/1250/1050 ms; GB/T may define its own. VERIFY. */
#define GBT_TP_T_CTS_MS   1250u
#define GBT_TP_T_ACK_MS   1250u
#define GBT_TP_DT_GAP_MS  10u      /* same 10 ms pacing as spoof tx_bam() */

void gbt_tp_tx_init(gbt_tp_tx_t *t, gbt_tp_send_fn send, bool use_bam);
bool gbt_tp_tx_start(gbt_tp_tx_t *t, uint32_t pgn, const uint8_t *payload, uint16_t len, uint32_t now_ms);
void gbt_tp_tx_on_rx(gbt_tp_tx_t *t, uint32_t id, const uint8_t *d, uint8_t dlc, uint32_t now_ms);
void gbt_tp_tx_poll(gbt_tp_tx_t *t, uint32_t now_ms);
bool gbt_tp_tx_busy(const gbt_tp_tx_t *t);
