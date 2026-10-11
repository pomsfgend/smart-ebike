#pragma once
/*
 * Fast-charge (FC) configuration: limits of the REAL low-voltage battery and of the
 * PSU power stack, plus the VIRTUAL high-voltage battery that is shown to the GB/T station.
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

/* ---------------- Power stack (see docs/DCDC_DESIGN.md) ----------------------------
 * One "string" = Eltek Flatpack2 48/2000 HE (CAN-controlled, 43.5-57.6 V, 2 kW, DC input
 * 85-275 V per datasheet) in SERIES with a 12 V HVDC-capable CRPS server PSU (180-300 V DC).
 * Vstack = Vfp + Vcrps. CC/CV is done by the Flatpack (CAN). Strings are paralleled.
 * Stage 1 = 1 string (~37 A), stage 2 = 2 strings (~74 A).                               */
#define FC_STACK_STRINGS              1
#define FC_STACK_STRINGS_MAX          3
#define FC_CRPS_V_NOM_0P1V            122    /* 12.2 V nominal, measured at runtime (VERIFY)   */
#define FC_CRPS_V_MIN_0P1V            115    /* below -> CRPS not running                       */
#define FC_CRPS_V_MAX_0P1V            128
#define FC_STRING_I_MAX_0P1A          370    /* 2000 W / 53.5 V = 37.4 A                         */
#define FC_STACK_VIN_MIN_0P1V         1900   /* PSU DC input window: FP2 185-275, CRPS 180-300  */
#define FC_STACK_VIN_MAX_0P1V         2700
#define FC_STACK_EFF_PCT              90     /* conservative, for the station current request   */
#define FC_PSU_T_DERATE_C             60     /* Flatpack outlet temp -> halve current (VERIFY)  */
#define FC_PSU_T_TRIP_C               75
#define FC_FP_DEFAULT_V_CV            4350   /* programmed fallback voltage of the Flatpack     */
#define FC_FP_OVP_CV                  5950

/* ---------------- Virtual HV battery presented to the station (by the cube) ----------
 * The station must stay inside the PSU DC input window. BHM/BCP "max allowed total
 * voltage" is therefore 275 V: a compliant station never exceeds it.                    */
#define FC_HV_REQ_V_0P1V              2500   /* preferred station voltage 250 V (CV mode)      */
#define FC_HV_RATED_V_0P1V            2500
#define FC_HV_MAX_V_0P1V              2750
#define FC_HV_I_MARGIN_PCT            15
#define FC_HV_I_MARGIN_MIN_0P1A       10
#define FC_HV_I_REQ_MAX_0P1A          (150 * FC_STACK_STRINGS)   /* 15 A per string @ 250 V */
#define FC_HV_BCL_MODE                0x01   /* 0x01 = constant voltage, 0x02 = constant current */


/* ---------------- DIRECT mode (primary since rev 3, docs/DIRECT_MODE.md) --------------
 * No DC-DC: the station output is connected to the battery through K-BAT. The station
 * is used as a current source: BCL requests a voltage >= CML min (a compliant 2023
 * station sends CST if BCL V < CML min, GB/T 27930-2023 11.3.1) and the CURRENT we want.
 * The battery clamps the bus to its own voltage; CV/taper is done by us via BCL current. */
#define FC_POWER_MODE_DIRECT          1
#define FC_POWER_MODE_DCDC            2
#ifndef FC_POWER_MODE
#define FC_POWER_MODE                 FC_POWER_MODE_DIRECT
#endif
#define FC_D_BCL_MODE                 0x02   /* CC: I = demand, V <= demand. Factory fw used 0x01 */
#define FC_D_BCP_VMAX_0P1V            1000   /* BHM/BCP max total V = 100 V (VERIFY per station)  */
#define FC_D_BCP_CELL_CV              365    /* 3.65 V in 0.01 V                                  */
#define FC_D_BCP_IMAX_0P1A            FC_TOTAL_I_MAX_0P1A
#define FC_D_I_USER_MAX_0P1A          FC_TOTAL_I_MAX_0P1A   /* web/UI cap, runtime-settable  */
#define FC_D_I_START_0P1A             50     /* first request after K1/K2 close: 5 A          */
#define FC_D_CV_KP_0P1A_PER_0P1V      20     /* CV loop: -2 A per 0.1 V above 65.7 V per step  */
#define FC_D_OCP_MARGIN_0P1A          50     /* soft OCP: I > limit + max(5 A, 10 %) ...       */
#define FC_D_OCP_PCT                  10
#define FC_D_OCP_SOFT_MS              1000   /* ... for 1 s -> cut request                     */
#define FC_D_OCP_HARD_MS              2000   /* ... for 2 s -> stop                            */
#define FC_D_STATION_OVERSHOOT_0P1A   50     /* CCS I > BCL I + 5 A + 10 % for 2 s -> stop     */
#define FC_D_INLET_SAFE_0P1V          600    /* <= 60 V: IMD finished/discharged (B.4.2)       */
#define FC_D_INLET_SETTLE_MS          500
#define FC_D_CLOSE_CHECK_MS           300    /* after K-BAT close: inlet must equal pack       */
#define FC_D_CLOSE_DV_0P1V            20     /* +-2 V                                          */
#define FC_D_MIN_I_STOP_MS            30000  /* need < CML imin for 30 s in CV -> stop (full)  */
#define FC_D_RAMP_SOFT_MS             2000   /* normal stop: wait |I| < 1 A up to 2 s          */
#define FC_D_RAMP_HARD_MS             300    /* fault stop: open after 300 ms (B.3.7.2)        */

/* ---------------- Timing ---------------------------------------------------------- */
#define FC_TASK_PERIOD_MS             100
#define FC_BMS_POLL_MS                250    /* per BMS                                         */
#define FC_BMS_STALE_MS               2000
#define FC_PSU_STALE_MS               1000   /* Flatpack status (sent ~every 200 ms, VERIFY)    */
#define FC_FP_LOGIN_PERIOD_MS         1000   /* must be < 15 s                                  */
#define FC_FP_SET_PERIOD_MS           200
/* Battery contactor "soft close": the PSUs themselves bring Vstack to Vbat (no precharge
   resistor needed). Flatpack current limit is held at FC_MATCH_I_0P1A while closing.       */
#define FC_MATCH_TIMEOUT_MS           10000
#define FC_MATCH_DV_0P1V              20     /* close K-BAT when Vbat <= Vstack <= Vbat + 2 V   */
#define FC_MATCH_I_0P1A               20     /* 2 A current limit while closing                 */
#define FC_WAIT_HV_TIMEOUT_MS         30000  /* CHARGING but PSUs see no input voltage          */
#define FC_RAMPDOWN_TIMEOUT_MS        2000   /* open K-BAT even if current did not reach < 1 A  */
#define FC_RAMPDOWN_MIN_MS            300    /* hold I = 0 setpoint at least this long          */
#define FC_OPEN_I_0P1A                10
/* link timeouts: see fc_link.h FCL_PAUSE_MS / FCL_STOP_MS */

/* ---------------- Charger controller pins: ESP32-C3 SuperMini ------------------------
 * Console = USB-Serial-JTAG (GPIO18/19), so UART0 and UART1 are both free for the BMSs. */
#define FC_PIN_VSTACK_ADC             0      /* ADC1_CH0. DCDC: Vstack 1:25.9. DIRECT: Vinlet 1:51  */
#if FC_POWER_MODE == FC_POWER_MODE_DIRECT
#define FC_VSENSE_DIV_X10             510    /* 3x330k + 20k -> 158 V full scale, clamp above   */
#else
#define FC_VSENSE_DIV_X10             259    /* 100k / 4.02k                                     */
#endif
#define FC_PIN_CC2_ADC                1      /* ADC1_CH1, optional CC2 sense (see HARDWARE.md)  */
#define FC_PIN_CRPS_PSON_N            2      /* via optocoupler; strap pull-up = PSU OFF         */
#define FC_PIN_KBAT_DRV               3      /* dynamic drive (1 kHz) -> charge pump -> coil    */
#define FC_PIN_CAN_TX                 4      /* TWAI -> ISO1050 -> Flatpack2 CAN (125 kbit/s)   */
#define FC_PIN_CAN_RX                 5
#define FC_PIN_BMS2_TX                6      /* UART1 -> isolator -> JBD #2                     */
#define FC_PIN_BMS2_RX                7
#define FC_PIN_HV_PERMIT_DRV          8      /* dynamic drive -> relay in series with K5/K6 coil */
#define FC_PIN_USER_BTN               9      /* BOOT button: press = user stop (active low)     */
#define FC_PIN_SPARE                  10     /* DCDC mode: spare. DIRECT mode: FC_PIN_HW_OVP_OK  */
#define FC_PIN_HW_OVP_OK              10     /* DIRECT: LOW = hardware OVP healthy (opto on);
                                                broken wire / tripped = HIGH (pull-up) = stop      */
#define FC_PIN_BMS1_TX                21     /* UART0 -> isolator -> JBD #1                     */
#define FC_PIN_BMS1_RX                20
#define FC_BMS_UART_BAUD              9600
#define FC_DRV_TOGGLE_HZ              1000   /* LEDC 50 % duty; static level = coil OFF          */
#define FC_CAN_BITRATE                125000
#define FC_WIFI_CHANNEL               6      /* must equal CUBE_WIFI_CHANNEL of the cube         */
