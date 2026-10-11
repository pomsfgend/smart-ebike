#pragma once
// Протокол JBD (Jiabaida / Xiaoxiang) BMS по UART 9600 8N1 (тот же формат идёт через BLE-характеристику FF01/FF02).
// Запрос:  DD A5 <cmd> 00 <chk_hi> <chk_lo> 77     cmd 0x03 — базовая информация, 0x04 — напряжения ячеек
// Ответ:   DD <cmd> <status> <len> <data…> <chk_hi> <chk_lo> 77
// Контрольная сумма = 0x10000 − Σ(байты от status/cmd(для запроса) до конца data).
// Чистый C++ без Arduino — покрыт тестами в firmware/test/test_native.
#include <stddef.h>
#include <stdint.h>

namespace jbd {

constexpr uint8_t kStart = 0xDD, kEnd = 0x77, kRead = 0xA5;
constexpr uint8_t kCmdBasic = 0x03, kCmdCells = 0x04, kCmdName = 0x05;

// Готовый кадр запроса (7 байт).
inline void buildRead(uint8_t cmd, uint8_t out[7]) {
  uint16_t sum = cmd + 0;           // cmd + len(0)
  uint16_t chk = (uint16_t)(0x10000 - sum);
  out[0] = kStart; out[1] = kRead; out[2] = cmd; out[3] = 0;
  out[4] = chk >> 8; out[5] = chk & 0xFF; out[6] = kEnd;
}

inline uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }

struct Basic {
  float    voltage_v, current_a;       // current: JBD >0 — заряд! (знак инвертируется в battery_meter)
  float    remain_ah, nominal_ah;
  uint16_t cycles, protection;
  uint8_t  rsoc, fet, cells, ntc;
  float    temp_c[4];
};

// Проверка кадра ответа. Возвращает указатель на data и её длину или nullptr.
inline const uint8_t* validate(const uint8_t* f, size_t n, uint8_t expectCmd, uint8_t& len) {
  if (n < 7 || f[0] != kStart || f[1] != expectCmd || f[2] != 0x00) return nullptr;
  len = f[3];
  if (n < (size_t)len + 7 || f[4 + len + 2] != kEnd) return nullptr;
  uint16_t sum = f[2] + f[3];
  for (uint8_t i = 0; i < len; ++i) sum += f[4 + i];
  uint16_t chk = (uint16_t)(0x10000 - sum);
  if (be16(f + 4 + len) != chk) return nullptr;
  return f + 4;
}

inline bool parseBasic(const uint8_t* f, size_t n, Basic& b) {
  uint8_t len; const uint8_t* d = validate(f, n, kCmdBasic, len);
  if (!d || len < 23) return false;
  b.voltage_v  = be16(d + 0) * 0.01f;
  b.current_a  = (int16_t)be16(d + 2) * 0.01f;
  b.remain_ah  = be16(d + 4) * 0.01f;
  b.nominal_ah = be16(d + 6) * 0.01f;
  b.cycles     = be16(d + 8);
  b.protection = be16(d + 16);
  b.rsoc       = d[19];
  b.fet        = d[20];               // bit0 — заряд, bit1 — разряд
  b.cells      = d[21];
  b.ntc        = d[22];
  for (uint8_t i = 0; i < 4; ++i) b.temp_c[i] = 0;
  for (uint8_t i = 0; i < b.ntc && i < 4 && 23 + 2 * i + 1 < len; ++i)
    b.temp_c[i] = ((int)be16(d + 23 + 2 * i) - 2731) * 0.1f;
  return true;
}

// Напряжения ячеек, мВ. Возвращает число ячеек или -1.
inline int parseCells(const uint8_t* f, size_t n, uint16_t* mv, uint8_t maxCells) {
  uint8_t len; const uint8_t* d = validate(f, n, kCmdCells, len);
  if (!d) return -1;
  int cnt = len / 2;
  if (cnt > maxCells) cnt = maxCells;
  for (int i = 0; i < cnt; ++i) mv[i] = be16(d + 2 * i);
  return cnt;
}
}  // namespace jbd
