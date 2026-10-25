#include "display.h"
#include <SPI.h>
#include "ui/theme.h"
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include "board.h"

static TFT_eSPI tft;
static uint8_t s_level = 0;

// The backlight driver has 16 steps; each LOW->HIGH pulse steps it down one level
// (wraps around). Ported from LilyGO UnitTest setBrightness().
void display_set_brightness(uint8_t value)
{
    const uint8_t steps = 16;
    if (value > steps) value = steps;
    if (value == 0) {
        digitalWrite(BOARD_TFT_BL, 0);
        delay(3);
        s_level = 0;
        return;
    }
    if (s_level == 0) {
        digitalWrite(BOARD_TFT_BL, 1);
        s_level = steps;
        delayMicroseconds(30);
    }
    int from = steps - s_level;
    int to = steps - value;
    int num = (steps + to - from) % steps;
    for (int i = 0; i < num; i++) {
        digitalWrite(BOARD_TFT_BL, 0);
        digitalWrite(BOARD_TFT_BL, 1);
    }
    s_level = value;
}

uint8_t display_get_brightness() { return s_level; }

static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    // The SD card and the LoRa radio share this bus and run SPI mode 0 (clock idles low);
    // the ST7789 runs mode 3 (clock idles high). On the ESP32-S3 a new mode only reaches
    // the clock pin when the SPI registers are latched, which TFT_eSPI does with the first
    // byte it sends, after it has already pulled the panel's chip select low. The panel
    // then sees the clock jump as a stray edge and the first block after an SD or radio
    // access is lost (#94). One throwaway byte in the panel's mode with every chip select
    // high puts the clock in the right place before the panel is selected.
    SPI.beginTransaction(SPISettings(SPI_FREQUENCY, MSBFIRST, TFT_SPI_MODE));
    SPI.transfer(0x00);
    SPI.endTransaction();
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)&color_p->full, w * h, false);
    tft.endWrite();
    lv_disp_flush_ready(disp);
}

void display_init()
{
    pinMode(BOARD_TFT_BL, OUTPUT);
    digitalWrite(BOARD_TFT_BL, 0);

    tft.begin();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    lv_init();

    // Two 40-line buffers in internal RAM: partial refresh is much cheaper than
    // the stock full-frame refresh and leaves PSRAM free for map tiles.
    static const size_t lines = 40;
    static lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(SCREEN_W * lines * sizeof(lv_color_t), MALLOC_CAP_DMA);
    static lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(SCREEN_W * lines * sizeof(lv_color_t), MALLOC_CAP_DMA);
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, SCREEN_W * lines);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_W;
    disp_drv.ver_res = SCREEN_H;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    theme_init(lv_disp_drv_register(&disp_drv));
}
