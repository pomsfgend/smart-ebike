// Тесты чистой логики на ПК: pio test -e native
#include <unity.h>
#include "../../lib/auto_light/auto_light_logic.h"
#include "../../lib/battery_meter/jbd_protocol.h"
#include "../../lib/battery_meter/soc_estimator.h"
#include "../../lib/buttons/buttons_logic.h"
#include "../../lib/display/screens.h"
#include "../../lib/power/power_logic.h"
#include "../../lib/grips/grips_logic.h"

using namespace auto_light;

void setUp() {}
void tearDown() {}

void test_auto_light_hysteresis() {
  Logic l({40, 120, 2000, 15000});
  TEST_ASSERT_FALSE(l.update(Switch::Auto, 30, 0));      // темно, но задержка
  TEST_ASSERT_TRUE(l.update(Switch::Auto, 30, 2000));    // включилось через 2 с
  TEST_ASSERT_TRUE(l.update(Switch::Auto, 80, 3000));    // в зоне гистерезиса — держим
  TEST_ASSERT_TRUE(l.update(Switch::Auto, 500, 4000));
  TEST_ASSERT_TRUE(l.update(Switch::Auto, 500, 18000));  // ещё < 15 с
  TEST_ASSERT_FALSE(l.update(Switch::Auto, 500, 19000)); // выключилось
}

void test_auto_light_switch_and_failsafe() {
  Logic l({40, 120, 2000, 15000});
  TEST_ASSERT_TRUE(l.update(Switch::On, 10000, 0));
  TEST_ASSERT_FALSE(l.update(Switch::Off, 0, 1));
  TEST_ASSERT_TRUE(l.update(Switch::Auto, -1, 2));       // датчик отвалился → свет вкл
}

void test_jbd_request_checksum() {
  uint8_t f[7]; jbd::buildRead(0x03, f);
  const uint8_t ref[7] = {0xDD, 0xA5, 0x03, 0x00, 0xFF, 0xFD, 0x77};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(ref, f, 7);
}

void test_jbd_cells_parse() {
  // DD 04 00 04 0E 42 0E 43 chk 77  (2 ячейки: 3650 и 3651 мВ)
  uint8_t f[] = {0xDD, 0x04, 0x00, 0x04, 0x0E, 0x42, 0x0E, 0x43, 0, 0, 0x77};
  uint16_t sum = 0x00 + 0x04 + 0x0E + 0x42 + 0x0E + 0x43; uint16_t c = 0x10000 - sum;
  f[8] = c >> 8; f[9] = c & 0xFF;
  uint16_t mv[4];
  TEST_ASSERT_EQUAL(2, jbd::parseCells(f, sizeof(f), mv, 4));
  TEST_ASSERT_EQUAL_UINT16(3650, mv[0]);
  f[5] ^= 1;  // порча данных → ошибка КС
  TEST_ASSERT_EQUAL(-1, jbd::parseCells(f, sizeof(f), mv, 4));
}

void test_soc_coulomb() {
  SocEstimator s({100, 18, 3.65f, 2, 0});
  s.setSoc(50);
  for (int i = 0; i < 3600; ++i) s.update(10, 57.6f, 1, -1);   // 10 А × 1 ч = 10 А·ч = 10 %
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 40.0f, s.soc());
}

void test_button_short_long() {
  buttons::Decoder d; buttons::Timing t{20, 800, 250};
  TEST_ASSERT_EQUAL(buttons::Event::None, d.update(true, 0, t, false));
  TEST_ASSERT_EQUAL(buttons::Event::Press, d.update(true, 25, t, false));
  TEST_ASSERT_EQUAL(buttons::Event::None, d.update(false, 100, t, false));   // дребезг
  TEST_ASSERT_EQUAL(buttons::Event::Short, d.update(false, 125, t, false));
  d.update(true, 200, t, false); d.update(true, 225, t, false);             // Press
  TEST_ASSERT_EQUAL(buttons::Event::Long, d.update(true, 1030, t, false));
  d.update(false, 1100, t, false);
  TEST_ASSERT_EQUAL(buttons::Event::Release, d.update(false, 1125, t, false));
}

void test_ladder_decode() {
  const uint16_t lv[] = {300, 750, 1200};
  TEST_ASSERT_EQUAL(1, buttons::ladderDecode(800, lv, 3, 150));
  TEST_ASSERT_EQUAL(-1, buttons::ladderDecode(3000, lv, 3, 150));
}

void test_screens_nav_and_charging() {
  ui::NavState n;
  ui::navigate(n, ui::Nav::Up);
  TEST_ASSERT_EQUAL((int)ui::Screen::Charging, (int)n.screen);
  ui::navigate(n, ui::Nav::Back);
  ui::autoSwitch(n, true, 0);
  TEST_ASSERT_EQUAL((int)ui::Screen::Charging, (int)n.screen);
  ui::autoSwitch(n, false, 0);
  TEST_ASSERT_EQUAL((int)ui::Screen::Main, (int)n.screen);
  TEST_ASSERT_EQUAL(100, ui::backlightPct(-1, 15, 100, 5000));
  TEST_ASSERT_EQUAL(15, ui::backlightPct(0.5f, 15, 100, 5000));
  TEST_ASSERT_EQUAL(60, ui::minutesToFull(50, 100, 50));   // 50 А·ч / 50 А = 1 ч
}

void test_power_precharge_sequence() {
  power::Logic l({200, 3000, 150, 200, 0.92f, 12, 0.35f, 10});
  power::Inputs in{}; in.vPack = 60; in.hasAux = true;
  l.update(in, 0);
  TEST_ASSERT_EQUAL((int)power::State::Off, (int)l.state());
  in.request = true;
  power::Outputs o = l.update(in, 10);
  TEST_ASSERT_EQUAL((int)power::State::Precharge, (int)l.state());
  in.vLoad = 56; o = l.update(in, 300);                 // 93 % → втягивание
  TEST_ASSERT_EQUAL((int)power::State::PullIn, (int)l.state());
  o = l.update(in, 310);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.2f, o.coilDuty);    // 12 В / 60 В
  in.auxClosed = true; o = l.update(in, 470);
  TEST_ASSERT_EQUAL((int)power::State::On, (int)l.state());
  o = l.update(in, 480);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.07f, o.coilDuty);   // удержание 35 %
  TEST_ASSERT_FALSE(o.precharge);
  in.kill = true; o = l.update(in, 500);
  TEST_ASSERT_EQUAL((int)power::State::Fault, (int)l.state());
  TEST_ASSERT_EQUAL(0.0f, o.coilDuty);
}

void test_power_precharge_timeout() {
  power::Logic l({200, 3000, 150, 200, 0.92f, 12, 0.35f, 10});
  power::Inputs in{}; in.vPack = 60; in.request = true; in.vLoad = 20;  // КЗ нагрузки
  l.update(in, 0); l.update(in, 3100);
  TEST_ASSERT_EQUAL((int)power::Fault::PrechargeTimeout, (int)l.fault());
}

void test_grips() {
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 25.0f, grips::ntcToC(1.65f));   // R = 10 кОм → 25 °C
  TEST_ASSERT_EQUAL(-999, (int)grips::ntcToC(0.0f));
  grips::Controller c({5, 30, 5, 55, 2.5f, 5, 60000, 15});
  c.setLevel(3, 0);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, c.target());
  TEST_ASSERT_TRUE(c.update(20, 80, true, 1, 100, 0.2f) > 0);
  TEST_ASSERT_EQUAL(0.0f, c.update(56, 80, true, 1, 200, 0.2f));   // перегрев
  TEST_ASSERT_EQUAL(0.0f, c.update(20, 10, true, 1, 300, 0.2f));   // низкий SoC
  TEST_ASSERT_EQUAL(0.0f, c.update(20, 80, true, 1, 60000, 0.2f)); // автоотключение
  TEST_ASSERT_EQUAL(0, c.level());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, grips::budgetDutyCap(5, 2.5f, 2));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_auto_light_hysteresis);
  RUN_TEST(test_auto_light_switch_and_failsafe);
  RUN_TEST(test_jbd_request_checksum);
  RUN_TEST(test_jbd_cells_parse);
  RUN_TEST(test_soc_coulomb);
  RUN_TEST(test_button_short_long);
  RUN_TEST(test_ladder_decode);
  RUN_TEST(test_screens_nav_and_charging);
  RUN_TEST(test_power_precharge_sequence);
  RUN_TEST(test_power_precharge_timeout);
  RUN_TEST(test_grips);
  return UNITY_END();
}
