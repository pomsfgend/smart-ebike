#include "battery_meter.h"
#include "config.h"
#include "config_store.h"
#include "i2c_bus.h"
#include "ina228.h"
#include "jbd_protocol.h"
#include "pins.h"
#include "soc_estimator.h"
#include "vehicle_state.h"
#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>

namespace {
constexpr uint8_t kInaAddr = 0x40;
INA228 g_ina;
SocEstimator g_soc({CFG_BATT_CAPACITY_AH, CFG_BATT_CELLS_S, CFG_CELL_V_FULL,
                    CFG_BATT_CAPACITY_AH / 50.0f, 0.5f});
static_assert(CFG_BATT_PACKS_P <= kMaxPacks, "too many packs");

struct BmsPort { HardwareSerial* ser; uint8_t rx, tx; };
BmsPort g_bms[CFG_BATT_PACKS_P] = {
  {&Serial0, pins::BMS_A_RX, pins::BMS_A_TX},
  {&Serial2, pins::BMS_B_RX, pins::BMS_B_TX},
};

// Чтение одного кадра JBD с таймаутом. Возвращает длину или 0.
size_t readFrame(HardwareSerial& s, uint8_t* buf, size_t cap, uint32_t timeoutMs) {
  uint32_t t0 = millis(); size_t n = 0;
  while (millis() - t0 < timeoutMs && n < cap) {
    if (!s.available()) { vTaskDelay(1); continue; }
    uint8_t c = s.read();
    if (n == 0 && c != jbd::kStart) continue;
    buf[n++] = c;
    if (n >= 4 && n == (size_t)buf[3] + 7) return n;
  }
  return 0;
}

bool queryBms(uint8_t idx, PackData& out) {
  HardwareSerial& s = *g_bms[idx].ser;
  uint8_t req[7], buf[128];
  while (s.available()) s.read();
  jbd::buildRead(jbd::kCmdBasic, req); s.write(req, 7);
  jbd::Basic b{};
  size_t n = readFrame(s, buf, sizeof(buf), 200);
  if (!n || !jbd::parseBasic(buf, n, b)) return false;

  out.voltage_v  = b.voltage_v;
  out.current_a  = -b.current_a;   // JBD: + заряд → у нас + разряд
  out.soc_pct    = b.rsoc;
  out.remain_ah  = b.remain_ah;
  out.nominal_ah = b.nominal_ah;
  out.cycles     = b.cycles;
  out.protection = b.protection;
  out.chgFet     = b.fet & 1;
  out.dsgFet     = b.fet & 2;
  out.cellCount  = b.cells;
  out.ntcCount   = b.ntc;
  for (uint8_t i = 0; i < 4; ++i) out.temp_c[i] = b.temp_c[i];

  jbd::buildRead(jbd::kCmdCells, req); s.write(req, 7);
  n = readFrame(s, buf, sizeof(buf), 200);
  int cnt = n ? jbd::parseCells(buf, n, out.cell_mv, kMaxCells) : -1;
  if (cnt > 0) {
    uint16_t mn = 0xFFFF, mx = 0;
    for (int i = 0; i < cnt; ++i) { mn = min(mn, out.cell_mv[i]); mx = max(mx, out.cell_mv[i]); }
    out.cellMin_mv = mn; out.cellMax_mv = mx;
  }
  out.lastUpdateMs = millis();
  out.online = true;
  return true;
}

// Средневзвешенный по ёмкости SoC сборок; -1 если ни одна BMS не на связи.
float bmsAverageSoc(const BatteryData& bd) {
  float sum = 0, cap = 0;
  for (uint8_t i = 0; i < CFG_BATT_PACKS_P; ++i) {
    const PackData& p = bd.packs[i];
    if (!p.online) continue;
    float c = p.nominal_ah > 0 ? p.nominal_ah : CFG_PACK_CAPACITY_AH;
    sum += p.soc_pct * c; cap += c;
  }
  return cap > 0 ? sum / cap : -1;
}

bool checkMismatch(const BatteryData& bd) {
  if (CFG_BATT_PACKS_P < 2 || !bd.packs[0].online || !bd.packs[1].online) return false;
  const PackData &a = bd.packs[0], &b = bd.packs[1];
  if (fabsf(a.soc_pct - b.soc_pct) > CFG_PACK_SOC_MISMATCH_PCT) return true;
  float tot = a.current_a + b.current_a;
  if (fabsf(tot) > 5.0f) {
    float share = a.current_a / tot;
    if (fabsf(share - 0.5f) > CFG_PACK_CURRENT_SHARE_TOL) return true;
  }
  return false;
}

float g_tripE0 = 0, g_tripQ0 = 0;
}  // namespace

namespace battery_meter {
void init() {
  I2cLock l;
  bool ok = g_ina.begin(Wire, kInaAddr, CFG_SHUNT_MOHM * 1e-3f, CFG_SHUNT_MAX_A, CFG_INA228_ADCRANGE);
  log_i("INA228 %s", ok ? "ok" : "NOT FOUND");
  for (auto& p : g_bms) p.ser->begin(9600, SERIAL_8N1, p.rx, p.tx);

  Preferences pr; pr.begin("batt", true);
  float saved = pr.getFloat("soc", -1); pr.end();
  if (saved >= 0) g_soc.setSoc(saved);   // иначе — инициализация от первой BMS
}

void resetTrip() {
  g_tripE0 = g_ina.energyJ(); g_tripQ0 = g_ina.chargeC();
}

void task(void*) {
  TickType_t last = xTaskGetTickCount();
  uint32_t lastBms = 0, lastSave = 0; uint8_t nextBms = 0;
  float lastSavedSoc = -100;
  for (;;) {
    VehicleState snap = vstate::snapshot();
    BatteryData bd = snap.battery;
    const float dt = 0.1f;

    if (millis() - lastBms >= CFG_BMS_POLL_MS / CFG_BATT_PACKS_P) {
      lastBms = millis();
      PackData& p = bd.packs[nextBms];
      if (!queryBms(nextBms, p) && millis() - p.lastUpdateMs > 5000) p.online = false;
      nextBms = (nextBms + 1) % CFG_BATT_PACKS_P;
    }

    bd.shuntOk = g_ina.ok();
    if (bd.shuntOk) {
      I2cLock l;
      bd.voltage_v = g_ina.busVoltage();
      bd.current_a = g_ina.current();
      bd.power_w   = bd.voltage_v * bd.current_a;
      bd.energy_wh = (g_ina.energyJ() - g_tripE0) / 3600.0f;
      bd.charge_ah = (g_ina.chargeC() - g_tripQ0) / 3600.0f;
      bd.temp_c    = g_ina.dieTemp();
    } else {
      // Резерв: сумма токов BMS и среднее напряжение (точность хуже, шаг 10 мА / 10 мВ, ~1 Гц).
      float i = 0, v = 0; uint8_t n = 0;
      for (uint8_t k = 0; k < CFG_BATT_PACKS_P; ++k)
        if (bd.packs[k].online) { i += bd.packs[k].current_a; v += bd.packs[k].voltage_v; ++n; }
      bd.current_a = i; bd.voltage_v = n ? v / n : 0; bd.power_w = bd.voltage_v * i;
    }

    g_soc.update(bd.current_a, bd.voltage_v, dt, bmsAverageSoc(bd));
    bd.soc_pct      = g_soc.soc();
    bd.remain_ah    = g_soc.remainingAh();
    bd.packMismatch = checkMismatch(bd);
    bd.charging     = bd.current_a < -1.0f;
    bd.valid        = g_soc.initialized();

    vstate::modify([&](VehicleState& s) { s.battery = bd; });

    // Сохранение SoC в NVS: при изменении ≥1 % или раз в 5 мин (ресурс flash).
    if (bd.valid && (fabsf(bd.soc_pct - lastSavedSoc) >= 1.0f || millis() - lastSave > 300000)) {
      Preferences pr; pr.begin("batt", false); pr.putFloat("soc", bd.soc_pct); pr.end();
      lastSavedSoc = bd.soc_pct; lastSave = millis();
    }
    vTaskDelayUntil(&last, pdMS_TO_TICKS(100));
  }
}
}  // namespace battery_meter
