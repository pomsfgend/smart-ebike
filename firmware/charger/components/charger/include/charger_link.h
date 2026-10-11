#pragma once
/* ESP-NOW transport of the FCL protocol (fc_link.h), charger side. */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "fc_link.h"

/* Peer MAC + LMK come from Kconfig (CONFIG_FC_PEER_MAC, CONFIG_FC_LMK) - see INTEGRATION doc. */
esp_err_t clink_init(void);
/* copy of last accepted STATION frame; returns its age in ms (UINT32_MAX if none yet) */
uint32_t clink_station(fcl_station_t *out);
void clink_send_request(fcl_request_t *r);   /* fills seq/ack */
/* If the cube is silent > 3 s and hopping is allowed (IDLE only), scan Wi-Fi channels 1..13:
   the cube's SoftAP follows the router channel when its STA side is connected (web.c APSTA). */
void clink_tick(uint32_t now_ms, bool allow_hop);
