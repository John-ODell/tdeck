#pragma once
#include <lvgl.h>

// Phosphor-green terminal palette. Every colour in the UI comes from here.
#define T_BG          lv_color_hex(0x000000)
#define T_SURFACE     lv_color_hex(0x03140a) // buttons, rows, status bar
#define T_SURFACE_HI  lv_color_hex(0x0a2e16) // own chat bubbles, map grid lines
#define T_TEXT        lv_color_hex(0x33ff66)
#define T_DIM         lv_color_hex(0x1c8c3c) // meta text, borders
#define T_ACCENT      lv_color_hex(0x00ff41) // focus background (text goes black)
#define T_ON_ACCENT   lv_color_hex(0x000000)
#define T_HEAD        lv_color_hex(0xd8ffe0) // rain head, "me" marker
#define T_WARN        lv_color_hex(0xffb030) // trackers, highlighted node
#define T_DANGER      lv_color_hex(0x5a0d0d)
#define T_DANGER_HI   lv_color_hex(0xc03a3a)

// Monospace body font (unscii 16 with Montserrat as fallback for LV_SYMBOL_* icons)
extern const lv_font_t *t_font_mono;

void theme_init(lv_disp_t *disp);
// Digital-rain splash over everything; removes itself after ms
void theme_rain_splash(uint32_t ms);
