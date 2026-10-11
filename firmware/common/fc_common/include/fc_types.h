#pragma once
#include <stdbool.h>
#include <stdint.h>

/* One JBD BMS snapshot (decoded from command 0x03 + 0x04). */
typedef struct {
    bool     valid;
    uint32_t rx_ms;            /* time of last good frame                        */
    uint16_t pack_mv_10;       /* total voltage, 10 mV units (JBD native)        */
    int32_t  current_10ma;     /* + charge, - discharge, 10 mA units             */
    uint16_t remain_10mah;
    uint16_t nominal_10mah;
    uint16_t cycles;
    uint16_t protection;       /* JBD protection bit field, 0 = OK               */
    uint8_t  soc_pct;
    uint8_t  fet;              /* bit0 charge MOSFET on, bit1 discharge MOSFET on */
    uint8_t  cell_count;
    uint8_t  ntc_count;
    int16_t  ntc_dc[8];        /* deci-degC                                      */
    uint16_t cell_mv[32];
    uint16_t cell_max_mv, cell_min_mv;
    uint8_t  cell_max_idx, cell_min_idx;  /* 1-based                             */
    bool     cells_valid;
} fc_bms_t;

/* Power-stack telemetry and command (Flatpack2 + CRPS, aggregated over strings). */
typedef struct {
    bool     valid;
    uint32_t rx_ms;
    uint16_t vin_0p1v;         /* PSU DC input = station voltage (Flatpack report) */
    uint16_t vout_0p1v;        /* stack output (ADC), before battery contactor     */
    int16_t  iout_0p1a;
    int16_t  temp_dc;
    bool     fault;            /* Flatpack alarm / CRPS not running              */
    bool     enabled;
} fc_dcdc_tel_t;

typedef struct {
    bool     enable;
    uint16_t vref_0p1v;        /* hardware CV setpoint                      */
    uint16_t iref_0p1a;        /* hardware CC setpoint (total)              */
} fc_dcdc_cmd_t;

/* What the station advertised (CML) and is doing (CCS). Received from the cube via the link. */
typedef struct {
    bool     cml_valid;
    uint16_t cml_vmax_0p1v, cml_vmin_0p1v;
    uint16_t cml_imax_0p1a, cml_imin_0p1a;  /* magnitudes, already sign-corrected */
    bool     ccs_valid;
    uint32_t ccs_ms;
    uint16_t ccs_v_0p1v;
    uint16_t ccs_i_0p1a;       /* magnitude */
    bool     ccs_permit;
} fc_station_t;

/* Request towards the station (goes into BCL). */
typedef struct {
    uint16_t v_0p1v;
    uint16_t i_0p1a;           /* magnitude; encoder applies the -400 A offset */
    uint8_t  mode;             /* 0x01 CV, 0x02 CC */
} fc_hv_request_t;

typedef enum {
    FC_STOP_NONE = 0,
    FC_STOP_FULL,              /* CV reached, current below termination       */
    FC_STOP_CELL_OV,
    FC_STOP_PACK_OV,
    FC_STOP_TEMP,
    FC_STOP_BMS_FAULT,         /* JBD protection flag or charge FET off       */
    FC_STOP_BMS_LOST,
    FC_STOP_DCDC_FAULT,
    FC_STOP_DCDC_LOST,
    FC_STOP_PACK_IMBALANCE,
    FC_STOP_STATION_RANGE,     /* station cannot supply a voltage inside the PSU DC input window */
    FC_STOP_CC2_LOST,
    FC_STOP_USER,
    FC_STOP_LINK_LOST,         /* cube link silent > FCL_STOP_MS               */
    FC_STOP_STATION_END,       /* station ended the session (CST/CEM) or cube stopping */
    FC_STOP_PRECHARGE,         /* battery-side precharge failed                */
} fc_stop_reason_t;

const char *fc_stop_reason_name(fc_stop_reason_t r);
