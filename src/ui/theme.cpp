#include "theme.h"
#include <Arduino.h>

static lv_font_t s_mono;
const lv_font_t *t_font_mono = &lv_font_montserrat_14;

static lv_theme_t s_theme;
static lv_style_t st_scr, st_surface, st_btn, st_focus, st_border, st_indicator, st_knob, st_ta;

static void apply_cb(lv_theme_t *, lv_obj_t *obj)
{
    if (!lv_obj_get_parent(obj)) {
        lv_obj_add_style(obj, &st_scr, 0);
        return;
    }
    if (lv_obj_check_type(obj, &lv_btn_class) || lv_obj_check_type(obj, &lv_list_btn_class)) {
        lv_obj_add_style(obj, &st_btn, 0);
        lv_obj_add_style(obj, &st_focus, LV_STATE_FOCUSED);
    } else if (lv_obj_check_type(obj, &lv_list_class)) {
        lv_obj_add_style(obj, &st_surface, 0);
    } else if (lv_obj_check_type(obj, &lv_textarea_class)) {
        lv_obj_add_style(obj, &st_ta, 0);
        lv_obj_add_style(obj, &st_border, LV_STATE_FOCUSED);
    } else if (lv_obj_check_type(obj, &lv_dropdown_class)) {
        lv_obj_add_style(obj, &st_btn, 0);
        lv_obj_add_style(obj, &st_focus, LV_STATE_FOCUSED);
    } else if (lv_obj_check_type(obj, &lv_dropdownlist_class)) {
        lv_obj_add_style(obj, &st_surface, 0);
        lv_obj_add_style(obj, &st_border, 0);
        lv_obj_add_style(obj, &st_focus, LV_PART_SELECTED | LV_STATE_CHECKED);
    } else if (lv_obj_check_type(obj, &lv_slider_class) || lv_obj_check_type(obj, &lv_switch_class)) {
        lv_obj_add_style(obj, &st_surface, 0);
        lv_obj_add_style(obj, &st_indicator, LV_PART_INDICATOR);
        lv_obj_add_style(obj, &st_indicator, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_style(obj, &st_knob, LV_PART_KNOB);
        lv_obj_add_style(obj, &st_border, LV_STATE_FOCUSED);
    }
}

void theme_init(lv_disp_t *disp)
{
    s_mono = lv_font_unscii_16;
    s_mono.fallback = &lv_font_montserrat_16;
    t_font_mono = &s_mono;

    lv_style_init(&st_scr);
    lv_style_set_bg_color(&st_scr, T_BG);
    lv_style_set_text_color(&st_scr, T_TEXT);
    lv_style_set_text_font(&st_scr, t_font_mono);

    lv_style_init(&st_surface);
    lv_style_set_bg_color(&st_surface, T_SURFACE);
    lv_style_set_text_color(&st_surface, T_TEXT);

    lv_style_init(&st_btn);
    lv_style_set_bg_color(&st_btn, T_SURFACE);
    lv_style_set_bg_opa(&st_btn, LV_OPA_COVER);
    lv_style_set_text_color(&st_btn, T_TEXT);
    lv_style_set_border_color(&st_btn, T_DIM);
    lv_style_set_border_width(&st_btn, 1);
    lv_style_set_shadow_width(&st_btn, 0);
    lv_style_set_radius(&st_btn, 2);

    lv_style_init(&st_focus);
    lv_style_set_bg_color(&st_focus, T_ACCENT);
    lv_style_set_bg_opa(&st_focus, LV_OPA_COVER);
    lv_style_set_text_color(&st_focus, T_ON_ACCENT);
    lv_style_set_outline_width(&st_focus, 0);

    lv_style_init(&st_border);
    lv_style_set_border_color(&st_border, T_ACCENT);
    lv_style_set_border_width(&st_border, 1);
    lv_style_set_outline_width(&st_border, 0);

    lv_style_init(&st_indicator);
    lv_style_set_bg_color(&st_indicator, T_DIM);

    lv_style_init(&st_knob);
    lv_style_set_bg_color(&st_knob, T_ACCENT);

    lv_style_init(&st_ta);
    lv_style_set_bg_color(&st_ta, T_BG);
    lv_style_set_text_color(&st_ta, T_TEXT);
    lv_style_set_border_color(&st_ta, T_DIM);
    lv_style_set_border_width(&st_ta, 1);
    lv_style_set_radius(&st_ta, 2);

    lv_theme_t *base = lv_theme_default_init(disp, lv_color_hex(0x00ff41), lv_color_hex(0x0a2e16), true, t_font_mono);
    s_theme = *base;
    lv_theme_set_parent(&s_theme, base);
    lv_theme_set_apply_cb(&s_theme, apply_cb);
    lv_disp_set_theme(disp, &s_theme);
}

// One label per column. Each frame rewrites a column's text with recolour tags:
// a bright head and a tail that fades to dark green.
static const int COLS = 40, ROWS = 15; // 8x16 cells on 320x240
struct Drop { float y, speed; uint8_t len; };
static Drop s_drops[COLS];
static lv_obj_t *s_cols[COLS];
static lv_obj_t *s_rain = nullptr;
static lv_timer_t *s_rain_timer = nullptr;
static uint32_t s_rain_end = 0;

static const char GLYPHS[] = "0123456789ABCDEFZ:.=*+<>|";

static void rain_frame(lv_timer_t *)
{
    static const uint32_t shades[] = {0xd8ffe0, 0x33ff66, 0x22cc4f, 0x179938, 0x0f6626, 0x083316};
    char buf[ROWS * 14 + 1];
    for (int c = 0; c < COLS; c++) {
        Drop &d = s_drops[c];
        d.y += d.speed;
        if (d.y - d.len > ROWS) { d.y = -(float)(esp_random() % 8); d.speed = 0.3f + (esp_random() % 70) / 100.0f; d.len = 4 + esp_random() % 9; }
        int head = (int)d.y;
        size_t o = 0;
        for (int r = 0; r < ROWS; r++) {
            int dist = head - r;
            if (dist < 0 || dist >= d.len) { buf[o++] = ' '; buf[o++] = '\n'; continue; }
            int shade = dist == 0 ? 0 : 1 + (dist * 5) / d.len;
            o += snprintf(buf + o, sizeof(buf) - o, "#%06x %c#\n", (unsigned)shades[shade > 5 ? 5 : shade],
                          GLYPHS[esp_random() % (sizeof(GLYPHS) - 1)]);
        }
        buf[o ? o - 1 : 0] = 0;
        lv_label_set_text(s_cols[c], buf);
    }
    if (millis() > s_rain_end) {
        lv_timer_del(s_rain_timer);
        s_rain_timer = nullptr;
        lv_obj_del(s_rain);
        s_rain = nullptr;
    }
}

void theme_rain_splash(uint32_t ms)
{
    if (s_rain) return;
    s_rain = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_rain);
    lv_obj_set_size(s_rain, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_rain, T_BG, 0);
    lv_obj_set_style_bg_opa(s_rain, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_rain, LV_OBJ_FLAG_SCROLLABLE);
    for (int c = 0; c < COLS; c++) {
        s_drops[c] = {-(float)(esp_random() % ROWS), 0.3f + (esp_random() % 70) / 100.0f, (uint8_t)(4 + esp_random() % 9)};
        s_cols[c] = lv_label_create(s_rain);
        lv_label_set_recolor(s_cols[c], true);
        lv_obj_set_style_text_font(s_cols[c], &lv_font_unscii_16, 0);
        lv_obj_set_style_text_line_space(s_cols[c], 0, 0);
        lv_obj_set_pos(s_cols[c], c * 8, 0);
        lv_label_set_text(s_cols[c], "");
    }
    s_rain_end = millis() + ms;
    s_rain_timer = lv_timer_create(rain_frame, 50, nullptr);
}
