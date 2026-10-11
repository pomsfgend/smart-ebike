#pragma once
/*
 * JBD (Jiabaida / "Xiaoxiang") BMS UART protocol, read-only use.
 *   request : DD A5 <cmd> 00 <chk_hi> <chk_lo> 77
 *   response: DD <cmd> <status> <len> <data...> <chk_hi> <chk_lo> 77
 *   checksum = 0x10000 - sum(bytes from <cmd>/<status> through last data byte)
 *   cmd 0x03 = basic info, 0x04 = cell voltages.
 * Basic info layout (big-endian): V 10mV | I 10mA signed | remain 10mAh | nominal 10mAh |
 *   cycles | date | balance lo | balance hi | protection | sw ver(1) | RSOC %(1) |
 *   FET(1: bit0 CHG, bit1 DSG) | cells(1) | NTC count(1) | NTC[n] in 0.1 K.
 * VERIFY against the documentation of the exact JBD model (UART vs RS485 vs BLE variant;
 * some firmware versions append extra bytes after the NTC block, which we ignore).
 * Pure C, host-tested. UART transport lives in jbd_bms_uart.c (ESP-IDF only).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fc_types.h"

#define JBD_CMD_BASIC 0x03u
#define JBD_CMD_CELLS 0x04u

size_t jbd_build_read(uint8_t *out /* >=7 */, uint8_t cmd);

typedef enum { JBD_OK = 0, JBD_NEED_MORE, JBD_BAD_FRAME, JBD_BAD_CHECKSUM, JBD_BMS_ERROR } jbd_result_t;
/* Parse one complete response in buf. On JBD_OK fills the relevant part of *bms. */
jbd_result_t jbd_parse(const uint8_t *buf, size_t len, fc_bms_t *bms, uint32_t now_ms);
