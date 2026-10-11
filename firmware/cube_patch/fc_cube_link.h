#pragma once
/*
 * Cube side of the FCL link (fc_link.h). New files for the FGMDCharge-FD-ROUND-V2 tree:
 *   main/fc_cube_link.c, main/fc_cube_link.h, main/fc_link.c, main/fc_link.h
 * (fc_link.[ch] are copied from firmware/common/fc_common). MIT, written from scratch; it only
 * calls the public app_state_* API of the cube and does not copy any cube code.
 * Wiring into sm_relay.c / web.c is described in docs/INTEGRATION_WITH_SPOOF.md section 5.
 *
 * Pinless: uses the radio the cube already runs (SoftAP "Cube", channel CUBE_WIFI_CHANNEL).
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "fc_link.h"

/* Call once after the Wi-Fi AP is started (web_start()). No-op if no peer configured. */
esp_err_t fc_cube_link_init(void);
bool fc_cube_link_enabled(void);                 /* peer MAC configured in settings      */

/* Call every tick (10 ms) from sm_relay_tick(): sends STATION at 10 Hz, applies the last
   REQUEST to app_state (BCL setpoint, BCS voltage/current, SOC). */
void fc_cube_link_tick(uint32_t now_ms);

uint32_t fc_cube_req_age(uint32_t now_ms);       /* UINT32_MAX if never                  */
bool fc_cube_ready_for_bro(uint32_t now_ms);     /* fresh REQUEST with READY             */
bool fc_cube_charge_allowed(uint32_t now_ms);    /* BSM byte 7 "charge allowed"          */
/* true -> cube must send BST and end the session. bst[4] is filled (reason, fault, error). */
bool fc_cube_stop_requested(uint32_t now_ms, uint8_t bst[4]);
/* BSM payload (7 bytes) built from real data, or false if the link is not fresh */
bool fc_cube_fill_bsm(uint32_t now_ms, uint8_t d[7]);
uint16_t fc_cube_bhm_vmax(void);                 /* 0.1 V; 1000 default (direct)         */
bool fc_cube_fill_bcp(uint32_t now_ms, uint8_t b[13]);   /* DIRECT mode BCP (FCL v2)    */
