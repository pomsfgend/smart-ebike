#include "config_store.h"
#include "config.h"
#include <Preferences.h>

namespace {
RuntimeConfig g_cfg{CFG_BATT_CAPACITY_AH, CFG_BATT_CELLS_S, CFG_AUTO_LUX_ON, CFG_AUTO_LUX_OFF, 1, 160};
Preferences prefs;
}

namespace config_store {
RuntimeConfig& get() { return g_cfg; }

void load() {
  prefs.begin("ebike", true);
  g_cfg.battCapacityAh = prefs.getFloat("capAh", g_cfg.battCapacityAh);
  g_cfg.battCells      = prefs.getUChar("cells", g_cfg.battCells);
  g_cfg.autoLuxOn      = prefs.getFloat("luxOn", g_cfg.autoLuxOn);
  g_cfg.autoLuxOff     = prefs.getFloat("luxOff", g_cfg.autoLuxOff);
  g_cfg.underglowMode  = prefs.getUChar("ugMode", g_cfg.underglowMode);
  g_cfg.ledBrightness  = prefs.getUChar("bright", g_cfg.ledBrightness);
  prefs.end();
}

void save() {
  prefs.begin("ebike", false);
  prefs.putFloat("capAh", g_cfg.battCapacityAh);
  prefs.putUChar("cells", g_cfg.battCells);
  prefs.putFloat("luxOn", g_cfg.autoLuxOn);
  prefs.putFloat("luxOff", g_cfg.autoLuxOff);
  prefs.putUChar("ugMode", g_cfg.underglowMode);
  prefs.putUChar("bright", g_cfg.ledBrightness);
  prefs.end();
}
}  // namespace config_store
