// display.h for the T-Deck Pro e-paper. LVGL renders as on the Plus; the flush converts
// each area to 1 bit into the display task's shadow frame and returns at once, so
// lv_timer_handler() never waits for the panel. The task decides when to draw.
#include "display.h"
#include <Arduino.h>
#include <lvgl.h>
#include "board.h"
#include "hal/epd_task.h"

static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint8_t *fb = epd::frame_lock();
    for (int y = area->y1; y <= area->y2; y++) {
        uint8_t *row = fb + y * epd::ROW_BYTES;
        for (int x = area->x1; x <= area->x2; x++, color_p++) {
            uint8_t bit = 0x80 >> (x & 7);
            if (lv_color_brightness(*color_p) >= 128) row[x >> 3] |= bit;
            else row[x >> 3] &= ~bit;
        }
    }
    epd::Rect r;
    r.x0 = area->x1; r.y0 = area->y1; r.x1 = area->x2; r.y1 = area->y2;
    epd::frame_unlock(r, lv_disp_flush_is_last(disp));
    lv_disp_flush_ready(disp);
}

void display_init()
{
    if (!epd::task_start()) Serial.println("display: e-paper not started");
    lv_init();
    // A 40-line render buffer is enough: the shadow frame holds the whole screen.
    static const size_t lines = 40;
    static lv_color_t *buf = (lv_color_t *)heap_caps_malloc(SCREEN_W * lines * sizeof(lv_color_t), MALLOC_CAP_INTERNAL);
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf, nullptr, SCREEN_W * lines);
    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.hor_res = SCREEN_W;
    drv.ver_res = SCREEN_H;
    drv.flush_cb = disp_flush;
    drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&drv);
}

// No backlight on the Pro; GPIO45's front light stays off. Kept for the shared API.
void display_set_brightness(uint8_t) {}
uint8_t display_get_brightness() { return 16; }
