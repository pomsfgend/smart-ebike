#pragma once
/*
 * Eltek Flatpack2 rectifier CAN protocol (community reverse-engineered, NOT an Eltek document):
 *   https://github.com/the6p4c/Flatpack2/blob/master/Protocol.md
 *   https://github.com/neggles/flatpack2s2/blob/main/docs/Protocol.md
 *   https://openinverter.org/forum/viewtopic.php?t=1351
 * 125 kbit/s, 29-bit IDs. The CAN bus is referenced to the PSU NEGATIVE OUTPUT rail
 * (in our series stack that rail sits ~12 V above battery minus -> isolated transceiver ISO1050).
 *
 *   RX 0x0500XXXX  "hello" every ~2 s: 0x1B, serial[6], 0x00
 *   TX 0x050048II  login (II = id*4), serial[6], 0x00, 0x00; repeat < 15 s (we do 1 s),
 *                  otherwise the PSU logs out and falls back to its DEFAULT voltage
 *   TX 0x05FF4004  set: I limit (0.1 A), V (0.01 V), V again, OVP (0.01 V)  (VERIFY id/broadcast)
 *   RX 0x05II40SS  status: Tin, I (0.1 A), Vout (0.01 V), Vin (1 V), Tout; SS 04 ok, 08 warn,
 *                  0C alarm, 10 walk-in
 *   TX 0x05II9C00  set default voltage: 0x29 0x15 0x00 V(0.01 V)  -> we program 43.50 V so a
 *                  logged-out PSU (charger ESP dead) cannot push the stack above the battery.
 * Everything here is VERIFY on the bench before connecting a battery.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FP2_V_MIN_CV     4350u    /* 43.50 V (datasheet 43.5-57.6 V)  */
#define FP2_V_MAX_CV     5760u
#define FP2_I_MAX_DA     417u     /* 41.7 A at 48 V, power-limited 2000 W */
#define FP2_P_MAX_W      2000u
#define FP2_OVP_CV       5950u

typedef struct {
    bool     seen;
    uint8_t  serial[6];
    uint8_t  id;              /* 1..63, assigned by login */
    bool     status_valid;
    uint32_t status_ms;
    uint8_t  state;           /* 0x04 ok, 0x08 warn, 0x0C alarm, 0x10 walk-in */
    uint16_t i_da;            /* output current, 0.1 A */
    uint16_t vout_cv;         /* output voltage, 0.01 V */
    uint16_t vin_v;           /* input voltage, 1 V (works for DC input as well, VERIFY) */
    int8_t   t_in_c, t_out_c;
} fp2_t;

typedef struct { uint32_t id; uint8_t dlc; uint8_t d[8]; } fp2_frame_t;

void fp2_build_login(fp2_frame_t *f, const fp2_t *p);
void fp2_build_set(fp2_frame_t *f, uint16_t i_da, uint16_t v_cv, uint16_t ovp_cv);
void fp2_build_default_v(fp2_frame_t *f, const fp2_t *p, uint16_t v_cv);
/* feed every received frame; returns true if it was a Flatpack2 frame */
bool fp2_on_rx(fp2_t *p, uint32_t id, const uint8_t *d, uint8_t dlc, uint32_t now_ms);
/* max current at a given output voltage (power limit) */
uint16_t fp2_i_limit_da(uint16_t v_cv);
