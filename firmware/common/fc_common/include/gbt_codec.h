#pragma once
/*
 * GB/T 27930-2015 (class A, also used by 2023 class A) message codec. Pure C, no ESP-IDF,
 * so it is unit-tested on the host (firmware/host_tests).
 *
 * Addresses: charger 0x56, vehicle/BMS 0xF4. 29-bit ID = P(3) R DP PF PS SA.
 * IDs below are the same numbers the spoof uses in main/can_gbt.h (GBT_ID_*), they are
 * repeated here with a GBTC_ prefix so this component builds without the spoof tree.
 *
 * Byte order: little-endian. Voltage 0.1 V/bit offset 0. Current 0.1 A/bit offset -400 A,
 * charging current is NEGATIVE, i.e. raw = 4000 - |I|*10.
 * Fields marked VERIFY: confirm against the text of GB/T 27930-2015 before relying on them.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GBTC_ADDR_CHARGER  0x56u
#define GBTC_ADDR_BMS      0xF4u

#define GBTC_ID(prio, pf, da, sa) \
    ((((uint32_t)(prio) & 7u) << 26) | ((uint32_t)(pf) << 16) | ((uint32_t)(da) << 8) | (uint32_t)(sa))
#define GBTC_PF(id)  ((uint8_t)(((id) >> 16) & 0xFFu))
#define GBTC_PS(id)  ((uint8_t)(((id) >> 8) & 0xFFu))
#define GBTC_SA(id)  ((uint8_t)((id) & 0xFFu))

/* PGN = PF << 8 for these PDU1 messages. */
enum {
    GBTC_PF_CRM = 0x01, GBTC_PF_BRM = 0x02, GBTC_PF_BCP = 0x06, GBTC_PF_CTS = 0x07,
    GBTC_PF_CML = 0x08, GBTC_PF_BRO = 0x09, GBTC_PF_CRO = 0x0A, GBTC_PF_BCL = 0x10,
    GBTC_PF_BCS = 0x11, GBTC_PF_CCS = 0x12, GBTC_PF_BSM = 0x13, GBTC_PF_BMV = 0x15,
    GBTC_PF_BMT = 0x16, GBTC_PF_BSP = 0x17, GBTC_PF_BST = 0x19, GBTC_PF_CST = 0x1A,
    GBTC_PF_BSD = 0x1C, GBTC_PF_CSD = 0x1D, GBTC_PF_BEM = 0x1E, GBTC_PF_CEM = 0x1F,
    GBTC_PF_CHM = 0x26, GBTC_PF_BHM = 0x27,
    GBTC_PF_TP_DT = 0xEB, GBTC_PF_TP_CM = 0xEC,
};

/* BMS -> charger */
#define GBTC_ID_BHM   GBTC_ID(6, GBTC_PF_BHM, 0x56, 0xF4)   /* 0x182756F4 */
#define GBTC_ID_BRO   GBTC_ID(4, GBTC_PF_BRO, 0x56, 0xF4)   /* 0x100956F4 */
#define GBTC_ID_BCL   GBTC_ID(6, GBTC_PF_BCL, 0x56, 0xF4)   /* 0x181056F4 */
#define GBTC_ID_BSM   GBTC_ID(6, GBTC_PF_BSM, 0x56, 0xF4)   /* 0x181356F4 */
#define GBTC_ID_BST   GBTC_ID(4, GBTC_PF_BST, 0x56, 0xF4)   /* 0x101956F4 */
#define GBTC_ID_BSD   GBTC_ID(6, GBTC_PF_BSD, 0x56, 0xF4)   /* 0x181C56F4 */
#define GBTC_ID_BEM   GBTC_ID(2, GBTC_PF_BEM, 0x56, 0xF4)   /* 0x081E56F4 */
#define GBTC_ID_TP_CM_BMS GBTC_ID(7, GBTC_PF_TP_CM, 0x56, 0xF4) /* 0x1CEC56F4 */
#define GBTC_ID_TP_DT_BMS GBTC_ID(7, GBTC_PF_TP_DT, 0x56, 0xF4) /* 0x1CEB56F4 */
/* charger -> BMS */
#define GBTC_ID_CHM   GBTC_ID(6, GBTC_PF_CHM, 0xF4, 0x56)   /* 0x1826F456 */
#define GBTC_ID_CRM   GBTC_ID(6, GBTC_PF_CRM, 0xF4, 0x56)   /* 0x1801F456 */
#define GBTC_ID_CTS   GBTC_ID(6, GBTC_PF_CTS, 0xF4, 0x56)   /* 0x1807F456 */
#define GBTC_ID_CML   GBTC_ID(6, GBTC_PF_CML, 0xF4, 0x56)   /* 0x1808F456 */
#define GBTC_ID_CRO   GBTC_ID(4, GBTC_PF_CRO, 0xF4, 0x56)   /* 0x100AF456 */
#define GBTC_ID_CCS   GBTC_ID(6, GBTC_PF_CCS, 0xF4, 0x56)   /* 0x1812F456 */
#define GBTC_ID_CST   GBTC_ID(4, GBTC_PF_CST, 0xF4, 0x56)   /* 0x101AF456 */
#define GBTC_ID_CSD   GBTC_ID(6, GBTC_PF_CSD, 0xF4, 0x56)   /* 0x181DF456 */
#define GBTC_ID_CEM   GBTC_ID(2, GBTC_PF_CEM, 0xF4, 0x56)   /* 0x081FF456 */
#define GBTC_ID_TP_CM_CHG GBTC_ID(7, GBTC_PF_TP_CM, 0xF4, 0x56) /* 0x1CECF456 */

/* Lengths (2015). */
#define GBTC_LEN_BRM  49
#define GBTC_LEN_BCP  13
#define GBTC_LEN_BCS  9

/* Current helpers: |I| in 0.1 A <-> raw with -400 A offset, charge negative. */
uint16_t gbtc_i_to_raw(uint16_t i_abs_0p1a);
uint16_t gbtc_raw_to_i_abs(uint16_t raw);   /* returns |raw - 4000| */

/* ---- encoders (BMS side). Return payload length. ---- */
size_t gbtc_enc_bhm(uint8_t *d, uint16_t vmax_0p1v);
typedef struct {
    uint8_t  ver[3];          /* 01 01 00 for 2015 (V1.1) as used by the spoof */
    uint8_t  battery_type;    /* 0x03 LFP, 0x06 NCM ... VERIFY table */
    uint16_t rated_cap_0p1ah;
    uint16_t rated_v_0p1v;
    char     vin[17];
} gbtc_brm_t;
size_t gbtc_enc_brm(uint8_t *d /* >=49 */, const gbtc_brm_t *b);
typedef struct {
    uint16_t cell_vmax_0p01v;
    uint16_t imax_0p1a;       /* magnitude */
    uint16_t energy_0p1kwh;
    uint16_t vmax_0p1v;
    int16_t  tmax_c;
    uint16_t soc_0p1pct;
    uint16_t vnow_0p1v;
} gbtc_bcp_t;
size_t gbtc_enc_bcp(uint8_t *d /* >=13 */, const gbtc_bcp_t *b);
size_t gbtc_enc_bro(uint8_t *d, bool ready);
size_t gbtc_enc_bcl(uint8_t *d, uint16_t v_0p1v, uint16_t i_abs_0p1a, uint8_t mode);
typedef struct {
    uint16_t v_0p1v;
    uint16_t i_abs_0p1a;
    uint16_t cell_vmax_0p01v; /* 12 bits */
    uint8_t  cell_group;      /* 4 bits */
    uint8_t  soc_pct;
    uint16_t remain_min;      /* never 0 while charging: many stations stop on 0 (spoof note) */
} gbtc_bcs_t;
size_t gbtc_enc_bcs(uint8_t *d /* >=9 */, const gbtc_bcs_t *b);
typedef struct {
    uint8_t cell_vmax_no;     /* 1-based */
    int16_t tmax_c; uint8_t tmax_no;
    int16_t tmin_c; uint8_t tmin_no;
    uint8_t cell_v_state;     /* 0 normal, 1 high, 2 low */
    uint8_t soc_state;        /* 0 normal, 1 high, 2 low */
    uint8_t overcurrent;      /* 0 normal, 1 over */
    uint8_t overtemp;         /* 0 normal, 1 over */
    uint8_t insulation;       /* 0 normal, 1 fault */
    uint8_t connector;        /* 0 normal, 1 fault */
    bool    charge_allowed;
} gbtc_bsm_t;
size_t gbtc_enc_bsm(uint8_t *d, const gbtc_bsm_t *b);
/* BST: reason byte0 (bit pairs), fault bytes 1..2, error byte3. VERIFY bit layout. */
size_t gbtc_enc_bst(uint8_t *d, uint8_t reason, uint16_t fault, uint8_t error);
typedef struct {
    uint8_t soc_pct; uint16_t cell_vmin_0p01v, cell_vmax_0p01v; int16_t tmin_c, tmax_c;
} gbtc_bsd_t;
size_t gbtc_enc_bsd(uint8_t *d, const gbtc_bsd_t *b);

/* BST byte0 reason pairs (01 = reached). */
#define GBTC_BST_SOC_REACHED    0x01u
#define GBTC_BST_VTOTAL_REACHED 0x04u
#define GBTC_BST_VCELL_REACHED  0x10u
#define GBTC_BST_CHARGER_STOP   0x40u
/* BST fault bits (2 bits each, 16-bit LE in bytes 1..2), VERIFY order. */
#define GBTC_BSTF_INSULATION    (1u << 0)
#define GBTC_BSTF_CONN_OT       (1u << 2)
#define GBTC_BSTF_BMS_OT        (1u << 4)
#define GBTC_BSTF_CONNECTOR     (1u << 6)
#define GBTC_BSTF_BATT_OT       (1u << 8)
#define GBTC_BSTF_HV_RELAY      (1u << 10)
#define GBTC_BSTF_CC2           (1u << 12)
#define GBTC_BSTF_OTHER         (1u << 14)

/* ---- decoders (charger side). Return false if too short. ---- */
typedef struct { uint8_t ver[3]; } gbtc_chm_t;
bool gbtc_dec_chm(const uint8_t *d, uint8_t len, gbtc_chm_t *o);
typedef struct { uint8_t result; uint32_t charger_no; } gbtc_crm_t;   /* 0x00 / 0xAA */
bool gbtc_dec_crm(const uint8_t *d, uint8_t len, gbtc_crm_t *o);
typedef struct { uint16_t vmax, vmin; uint16_t imax_abs, imin_abs; bool has_imin; } gbtc_cml_t;
bool gbtc_dec_cml(const uint8_t *d, uint8_t len, gbtc_cml_t *o);
bool gbtc_dec_cro(const uint8_t *d, uint8_t len, bool *ready);
typedef struct { uint16_t v_0p1v, i_abs_0p1a, time_min; uint8_t permit; bool has_permit; } gbtc_ccs_t;
bool gbtc_dec_ccs(const uint8_t *d, uint8_t len, gbtc_ccs_t *o);
typedef struct { uint8_t reason; uint16_t fault; uint8_t error; } gbtc_cst_t;
bool gbtc_dec_cst(const uint8_t *d, uint8_t len, gbtc_cst_t *o);
typedef struct { uint16_t time_min, energy_0p1kwh; uint32_t charger_no; } gbtc_csd_t;
bool gbtc_dec_csd(const uint8_t *d, uint8_t len, gbtc_csd_t *o);
