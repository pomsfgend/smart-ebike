#pragma once
/*
 * Cube <-> charger link protocol ("FCL"), version 2 (v2 = direct mode fields, see
 * docs/DIRECT_MODE.md; v1 frames are rejected with FCL_E_VERSION).
 *
 *   Cube  (FGMDCharge, ESP32-C3, talks to the station)   --STATION 10 Hz-->  charger ESP
 *   Charger ESP (BMS, PSUs, battery contactor)           --REQUEST 10 Hz-->  cube
 *
 * Transport: ESP-NOW, unicast between two paired MACs, encrypted peer (LMK), on the Wi-Fi
 * channel of the cube SoftAP (CUBE_WIFI_CHANNEL = 6). Payload <= 250 bytes (ESP-NOW limit).
 * The same frames can be carried over any byte transport (UART, private CAN ID) because they
 * are self-delimiting: magic, version, type, length, CRC-16/CCITT-FALSE.
 *
 * Serialisation is explicit little-endian (no packed structs), so both firmwares and the host
 * tests agree byte-for-byte. Units follow GB/T: 0.1 V, 0.1 A (magnitudes, NO -400 A offset here).
 *
 * Failsafe (both sides, see docs/INTEGRATION_WITH_SPOOF.md §4):
 *   age > FCL_PAUSE_MS : treat as "no power allowed" (charger: current 0; cube: BCL I = 0,
 *                        BSM "charge not allowed")
 *   age > FCL_STOP_MS  : stop (charger: latch stop, open battery contactor after I < 1 A;
 *                        cube: BST "other fault", end session, open HV relays)
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FCL_MAGIC0      0x46u   /* 'F' */
#define FCL_MAGIC1      0x43u   /* 'C' */
#define FCL_VERSION     2u
#define FCL_PERIOD_MS   100u
#define FCL_PAUSE_MS    300u
#define FCL_STOP_MS     1000u
#define FCL_MAX_LEN     64u

typedef enum { FCL_T_STATION = 1, FCL_T_REQUEST = 2 } fcl_type_t;

/* session as seen by the cube (mapped from its ST_* states) */
typedef enum {
    FCL_SESS_IDLE = 0, FCL_SESS_HANDSHAKE = 1, FCL_SESS_PARAM = 2,
    FCL_SESS_READY = 3, FCL_SESS_CHARGING = 4, FCL_SESS_STOPPING = 5,
} fcl_session_t;

/* STATION.flags */
#define FCL_SF_CAN_ALIVE   (1u << 0)   /* station frames seen in the last 1 s          */
#define FCL_SF_CML_VALID   (1u << 1)
#define FCL_SF_CCS_VALID   (1u << 2)   /* CCS younger than 1 s                         */
#define FCL_SF_CCS_PERMIT  (1u << 3)   /* CCS permit bits == 01                        */
#define FCL_SF_CRO_READY   (1u << 4)
#define FCL_SF_RELAY_A     (1u << 5)   /* cube GPIO6 (HV precharge)                    */
#define FCL_SF_RELAY_B     (1u << 6)   /* cube GPIO7 (HV main, K5/K6)                  */
#define FCL_SF_STATION_END (1u << 7)   /* CST / CEM / CSD seen                         */

typedef struct {
    uint16_t seq;
    uint16_t ack;            /* last REQUEST seq received                         */
    uint8_t  session;        /* fcl_session_t                                      */
    uint8_t  protocol;       /* cube app_state protocol: 1 = 2015/A, 2 = 2011, 3 = 2023-B */
    uint8_t  flags;          /* FCL_SF_*                                           */
    uint8_t  cst_reason;     /* CST byte 0 if seen                                 */
    uint16_t cml_vmax, cml_vmin, cml_imax, cml_imin;   /* 0.1 V / 0.1 A            */
    uint16_t ccs_v, ccs_i;   /* station output, 0.1 V / 0.1 A                      */
    uint32_t uptime_ms;
} fcl_station_t;

/* REQUEST.flags */
#define FCL_RF_READY       (1u << 0)   /* battery side ready -> cube may send BRO 0xAA  */
#define FCL_RF_ALLOW       (1u << 1)   /* charge allowed (BSM byte 7 bits 5-6 = 01)    */
#define FCL_RF_STOP        (1u << 2)   /* request session stop with bst_* below        */
#define FCL_RF_HV_PERMIT   (1u << 3)   /* charger permit for K5/K6 (also hard-wired)   */
#define FCL_RF_DIRECT      (1u << 4)   /* direct mode: BCP/BCS use batt_v/batt_i/bcp_* verbatim */

typedef struct {
    uint16_t seq;
    uint16_t ack;            /* last STATION seq received                          */
    uint8_t  flags;          /* FCL_RF_*                                           */
    uint8_t  bcl_mode;       /* 0x01 CV, 0x02 CC                                    */
    uint16_t bcl_v, bcl_i;   /* request to the station (HV side)                   */
    uint16_t bcs_v, bcs_i;   /* "measured" values for BCS (HV side, from PSU telemetry) */
    uint16_t bhm_vmax;       /* BHM max allowed V = BCP max total charge voltage   */
    uint16_t cell_vmax_mv;   /* real                                               */
    uint8_t  cell_vmax_no;
    int8_t   tmax_c, tmin_c;
    uint8_t  tmax_no, tmin_no;
    uint8_t  soc;            /* %                                                  */
    uint16_t remain_min;
    uint8_t  bsm_byte5;      /* BSM byte 6 (cell V / SOC / overcurrent / overtemp)  */
    uint8_t  bst_reason;
    uint16_t bst_fault;
    uint8_t  bst_error;
    uint16_t batt_v;         /* real battery voltage, 0.1 V (direct: BCP SPN2822 + BCS) */
    int16_t  batt_i;         /* real LV battery, 0.1 A (+ = charge)                 */
    /* v2 */
    uint16_t bcp_imax;       /* BCP max allowed charge current, 0.1 A magnitude    */
    uint16_t bcp_cell_cv;    /* BCP max allowed cell voltage, 0.01 V (365 = 3.65 V) */
} fcl_request_t;

uint16_t fcl_crc16(const uint8_t *d, size_t n);   /* CRC-16/CCITT-FALSE */

size_t fcl_pack_station(uint8_t *out, const fcl_station_t *s);
size_t fcl_pack_request(uint8_t *out, const fcl_request_t *r);

typedef enum { FCL_OK = 0, FCL_E_SHORT, FCL_E_MAGIC, FCL_E_VERSION, FCL_E_TYPE, FCL_E_CRC } fcl_err_t;
fcl_err_t fcl_unpack(const uint8_t *in, size_t n, fcl_type_t *type, fcl_station_t *s, fcl_request_t *r);

/* Link freshness / replay guard */
typedef struct {
    bool     ever;
    uint32_t last_ms;
    uint16_t last_seq;
    uint32_t ok, bad, dup;
} fcl_monitor_t;

/* Accept a frame with this seq at now; returns false for duplicates/old (replay). */
bool fcl_monitor_accept(fcl_monitor_t *m, uint16_t seq, uint32_t now_ms);
uint32_t fcl_monitor_age(const fcl_monitor_t *m, uint32_t now_ms);   /* UINT32_MAX if never */
