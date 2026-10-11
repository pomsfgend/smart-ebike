#pragma once
/*
 * Fast-charge (FC) configuration: limits of the REAL low-voltage battery and of the
 * DC-DC converter, plus the VIRTUAL high-voltage battery that is shown to the GB/T station.
 *
 * Naming follows the spoof project (FGMDCharge config.h uses CUBE_*, values in 0.1 V / 0.1 A).
 * Everything marked VERIFY must be checked against the datasheets / the real hardware.
 *
 * Units: _0P1V = 0.1 V, _0P1A = 0.1 A, _MV = mV, _DC = deci-degC (0.1 C).
 */

/* ---------------- Real battery: 2 packs LiFePO4 18S in parallel, each with JBD BMS ---- */
#define FC_PACK_COUNT                 2
#define FC_CELLS_SERIES               18
#define FC_CELL_CV_MV                 3650   /* 18 x 3.65 V = 65.7 V (user spec)              */
#define FC_CELL_TAPER_START_MV        3550   /* start reducing current per-cell (VERIFY)      */
#define FC_CELL_ABS_MAX_MV            3700   /* hard stop, below JBD OVP (VERIFY BMS setting) */
#define FC_CELL_MIN_CHARGE_MV         2500   /* below: precharge-current only                 */
#define FC_PACK_CV_0P1V               657    /* 65.7 V                                         */
#define FC_PACK_OVP_0P1V              666    /* 66.6 V hard stop (= 3.70 V/cell)              */

/* JBD BMS rating (user confirmed): 60 A continuous, 70 A peak, charge == discharge. */
#define FC_BMS_RATED_CONT_0P1A        600
#define FC_BMS_RATED_PEAK_0P1A        700
/* Working limit per pack: 50 A = 83 % of continuous rating. */
#define FC_PACK_I_MAX_0P1A            500
#define FC_TOTAL_I_MAX_0P1A           (FC_PACK_I_MAX_0P1A * FC_PACK_COUNT) /* 100 A */
#define FC_LOW_CELL_I_0P1A            50     /* 5 A while any cell < FC_CELL_MIN_CHARGE_MV  */
#define FC_TERM_I_0P1A                30     /* end of CV: total current < 3 A (VERIFY ~0.02-0.05C) */
#define FC_TERM_HOLD_MS               60000  /* current must stay below term for 60 s          */
#define FC_I_SLEW_0P1A_PER_S          100    /* +10 A/s ramp up, down is immediate             */
#define FC_PACK_IMBALANCE_0P1V        10     /* refuse parallel start if packs differ > 1.0 V  */

/* LiFePO4 charge temperature derating (cell NTC from JBD), deci-degC. VERIFY with cell datasheet */
#define FC_T_CHG_MIN_DC               0      /* < 0 C   : charging forbidden                  */
#define FC_T_COLD_DC                  100    /* 0..10 C : 20 % current                         */
#define FC_T_COOL_DC                  150    /* 10..15 C: 50 % current                         */
#define FC_T_WARM_DC                  450    /* 15..45 C: 100 %                                */
#define FC_T_HOT_DC                   500    /* 45..50 C: 50 %                                 */
#define FC_T_CHG_MAX_DC               550    /* 50..55 C: 20 %, > 55 C forbidden               */

/* ---------------- DC-DC converter (see docs/DCDC_DESIGN.md) ------------------------- */
#define FC_DCDC_MODULES               2      /* 2 x 3.3 kW identical modules                   */
#define FC_DCDC_VIN_MIN_0P1V          2500   /* converter input window 250..500 V              */
#define FC_DCDC_VIN_MAX_0P1V          5000
#define FC_DCDC_POUT_MAX_W            6600
#define FC_DCDC_EFF_PCT               92     /* for station current request, conservative     */
#define FC_DCDC_T_DERATE_DC           750    /* heatsink 75 C -> start derating                */
#define FC_DCDC_T_TRIP_DC             900    /* 90 C -> stop                                   */

/* ---------------- Virtual HV battery presented to the station (GB/T side) ------------ */
#define FC_HV_REQ_V_0P1V              4000   /* preferred station voltage 400 V (CV mode)      */
#define FC_HV_RATED_V_0P1V            4000   /* BRM rated total voltage                         */
#define FC_HV_MAX_V_0P1V              4500   /* BHM / BCP max allowable total voltage 450 V    */
#define FC_HV_I_MARGIN_PCT            15     /* station current request margin                 */
#define FC_HV_I_MARGIN_MIN_0P1A       10     /* + at least 1 A                                 */
#define FC_HV_I_REQ_MAX_0P1A          330    /* never ask more than 33 A from the station     */
#define FC_HV_BCL_MODE                0x01   /* 0x01 = constant voltage, 0x02 = constant current */
#define FC_HV_CAPACITY_0P1AH          1000   /* BRM rated capacity (virtual, VERIFY real Ah)  */
#define FC_HV_ENERGY_0P1KWH           100    /* BCP nominal energy (virtual, VERIFY)          */

/* ---------------- Timing ---------------------------------------------------------- */
#define FC_TASK_PERIOD_MS             100    /* supervisor loop                                */
#define FC_BMS_POLL_MS                250    /* per BMS, alternating -> each BMS every 500 ms  */
#define FC_BMS_STALE_MS               2000   /* no fresh BMS data -> current 0, then stop     */
#define FC_DCDC_STALE_MS              500

/* ---------------- Pins: ESP32-C3 cube has only GPIO0 and GPIO3 free ---------------- */
/* Shared I2C with the CST816S touch (GPIO1 SDA / GPIO8 SCL, addr 0x15). */
#define FC_I2C_PORT                   0
#define FC_I2C_ADDR_DAC_MCP4728       0x60   /* 4-ch 12-bit DAC: Iref1, Iref2, Vref, spare     */
#define FC_I2C_ADDR_ADC_ADS1115       0x48   /* #1: CC2, Vin, Vout, T module 1 (VERIFY addr)   */
#define FC_I2C_ADDR_ADC2_ADS1115      0x49   /* #2: IMON1, IMON2, T module 2, spare (TODO)      */
#define FC_I2C_ADDR_IO_TCA9554        0x20   /* EN, FAULT, BMS mux select, K-contactor fb       */
#define FC_BMS_UART_NUM               1      /* UART1, UART0 stays the console                 */
#define FC_BMS_UART_TX_GPIO           0
#define FC_BMS_UART_RX_GPIO           3
#define FC_BMS_UART_BAUD              9600   /* JBD default (VERIFY)                            */
/* Existing spoof relays: GPIO6 "prepare" (A), GPIO7 "load" (B), HIGH = on. */
