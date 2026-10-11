// Бэкенд LVGL 8.3 + LovyanGFX для SPI TFT 480×320 (ILI9488 по умолчанию / ST7796S).
// Собирается в окружении esp32s3_lvgl (platformio.ini), где заданы -DUSE_LVGL и зависимости.
// Скелет: инициализация панели, буферы LVGL, flush-callback. Вёрстка экранов — задача DISP-04.
#include "display_hal.h"
#include "config.h"
#include "pins.h"

#ifdef USE_LVGL
#include <Arduino.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lvgl.h>

namespace {
class LGFX : public lgfx::LGFX_Device {
#if CFG_DISPLAY_TYPE == DISPLAY_ST7796S_SPI
  lgfx::Panel_ST7796 panel_;
#else
  lgfx::Panel_ILI9488 panel_;
#endif
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;

 public:
  LGFX() {
    auto b = bus_.config();
    b.spi_host = SPI2_HOST;          // FSPI
    b.spi_mode = 0;
    b.freq_write = CFG_DISPLAY_SPI_HZ;
    b.freq_read = 16000000;
    b.pin_sclk = pins::TFT_SCK;
    b.pin_mosi = pins::TFT_MOSI;
    b.pin_miso = -1;                 // ILI9488: SDO не отпускает линию — не подключаем
    b.pin_dc = pins::TFT_DC;
    b.dma_channel = SPI_DMA_CH_AUTO;
    bus_.config(b);
    panel_.setBus(&bus_);

    auto p = panel_.config();
    p.pin_cs = pins::TFT_CS;
    p.pin_rst = -1;                  // сброс от EN/RC или PCA9555
    p.panel_width = 320;             // родная ориентация — портрет
    p.panel_height = 480;
    p.readable = false;
    p.bus_shared = false;
    panel_.config(p);

    auto l = light_.config();
    l.pin_bl = pins::TFT_BL;
    l.freq = 12000;                  // выше слышимого диапазона, без мерцания на камеру
    l.pwm_channel = 7;
    light_.config(l);
    panel_.setLight(&light_);
    setPanel(&panel_);
  }
};

LGFX g_tft;
lv_disp_draw_buf_t g_drawBuf;
lv_color_t* g_buf1 = nullptr;
lv_color_t* g_buf2 = nullptr;

void flushCb(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* px) {
  uint32_t w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
  g_tft.startWrite();
  g_tft.setAddrWindow(a->x1, a->y1, w, h);
  g_tft.writePixels((lgfx::rgb565_t*)px, w * h);   // для ILI9488 LovyanGFX сам конвертирует в RGB666
  g_tft.endWrite();
  lv_disp_flush_ready(drv);
}

class LvglDisplay : public DisplayHal {
 public:
  bool begin() override {
    g_tft.init();
    g_tft.setRotation(CFG_DISPLAY_ROTATION);
    lv_init();
    size_t n = CFG_DISPLAY_W * CFG_DISPLAY_BUF_LINES;
    g_buf1 = (lv_color_t*)heap_caps_malloc(n * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    g_buf2 = (lv_color_t*)heap_caps_malloc(n * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!g_buf1 || !g_buf2) return false;
    lv_disp_draw_buf_init(&g_drawBuf, g_buf1, g_buf2, n);
    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.hor_res = CFG_DISPLAY_W;
    drv.ver_res = CFG_DISPLAY_H;
    drv.flush_cb = flushCb;
    drv.draw_buf = &g_drawBuf;
    lv_disp_drv_register(&drv);
    buildScreens();
    return true;
  }
  void setBacklight(uint8_t pct) override { g_tft.setBrightness((uint8_t)(pct * 255 / 100)); }
  const char* name() const override { return "lvgl"; }
  void render(ui::Screen sc, const VehicleState& s, uint32_t now) override {
    if (sc != shown_) { lv_scr_load(scr_[(int)sc]); shown_ = sc; }
    char buf[48];
    snprintf(buf, sizeof(buf), "%.0f", s.speedKmh);       lv_label_set_text(speed_, buf);
    snprintf(buf, sizeof(buf), "%.0f%%", s.battery.soc_pct); lv_label_set_text(soc_, buf);
    // TODO(DISP-04): остальные экраны (Battery, Packs, Lights, Trip, Charging), иконки, цвета предупреждений.
    lv_timer_handler();
    (void)now;
  }

 private:
  void buildScreens() {
    for (int i = 0; i < (int)ui::Screen::Count; ++i) scr_[i] = lv_obj_create(nullptr);
    lv_obj_t* m = scr_[(int)ui::Screen::Main];
    lv_obj_set_style_bg_color(m, lv_color_black(), 0);     // тёмная тема: меньше бликов ночью
    speed_ = lv_label_create(m);
    lv_obj_set_style_text_font(speed_, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(speed_, lv_color_white(), 0);
    lv_obj_align(speed_, LV_ALIGN_CENTER, 0, -30);
    soc_ = lv_label_create(m);
    lv_obj_set_style_text_font(soc_, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(soc_, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_align(soc_, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_scr_load(m);
  }
  lv_obj_t* scr_[(int)ui::Screen::Count] = {};
  lv_obj_t* speed_ = nullptr;
  lv_obj_t* soc_ = nullptr;
  ui::Screen shown_ = ui::Screen::Main;
};
}  // namespace

DisplayHal& displayBackend() { static LvglDisplay d; return d; }
#endif  // USE_LVGL
