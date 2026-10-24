// Timer: a stopwatch with laps and a countdown that rings the speaker. Both keep running
// with the app closed; the state lives here and an LVGL timer watches the countdown.
#include "apps.h"
#include <Arduino.h>
#include "hal/audio.h"
#include "ui/theme.h"

enum Mode { STOPWATCH, COUNTDOWN };
static Mode s_mode = STOPWATCH;

// Stopwatch
static bool sw_running = false;
static uint32_t sw_start = 0, sw_acc = 0;     // ms
static uint32_t sw_laps[4];
static int sw_lap_count = 0;

// Countdown
static uint32_t cd_set = 5 * 60 * 1000;       // ms, what up/down adjust
static uint32_t cd_left = cd_set;             // ms left while paused
static uint32_t cd_end = 0;                   // millis() when it reaches zero
static bool cd_running = false;
static int cd_rings = 0;                      // alarm rounds still to ring
static uint32_t cd_last_ring = 0;

static lv_obj_t *s_root = nullptr, *s_tabs[2], *s_big, *s_sub, *s_hint;
static lv_timer_t *s_watch = nullptr;         // outlives the app

static uint32_t sw_elapsed() { return sw_acc + (sw_running ? millis() - sw_start : 0); }
static uint32_t cd_remaining()
{
    if (!cd_running) return cd_left;
    int32_t d = (int32_t)(cd_end - millis());
    return d > 0 ? (uint32_t)d : 0;
}

static void fmt_ms(uint32_t ms, char *out, size_t cap, bool tenths)
{
    uint32_t s = ms / 1000, h = s / 3600, m = (s / 60) % 60;
    if (h) snprintf(out, cap, "%lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)(s % 60));
    else if (tenths) snprintf(out, cap, "%02lu:%02lu.%lu", (unsigned long)m, (unsigned long)(s % 60), (unsigned long)((ms / 100) % 10));
    else snprintf(out, cap, "%02lu:%02lu", (unsigned long)m, (unsigned long)(s % 60));
}

static void refresh()
{
    if (!s_root) return;
    char t[16];
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_bg_color(s_tabs[i], i == s_mode ? T_ACCENT : T_SURFACE, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(s_tabs[i], 0), i == s_mode ? lv_color_black() : T_DIM, 0);
    }
    if (s_mode == STOPWATCH) {
        fmt_ms(sw_elapsed(), t, sizeof(t), true);
        lv_label_set_text(s_big, t);
        String laps;
        for (int i = sw_lap_count - 1; i >= 0; i--) {
            char l[24];
            fmt_ms(sw_laps[i], l, sizeof(l), true);
            laps += String("lap ") + (i + 1) + "  " + l + "\n";
        }
        lv_label_set_text(s_sub, laps.c_str());
        lv_label_set_text(s_hint, sw_running ? "click = pause   l = lap" : sw_elapsed() ? "click = resume   r = reset" : "click = start");
    } else {
        uint32_t left = cd_remaining();
        fmt_ms(left, t, sizeof(t), false);
        lv_label_set_text(s_big, t);
        char set[32];
        fmt_ms(cd_set, set, sizeof(set), false);
        lv_label_set_text_fmt(s_sub, "set for %s", set);
        lv_label_set_text(s_hint, cd_rings ? "ringing: any key stops it" :
                          cd_running ? "click = pause   r = reset" :
                          left && left != cd_set ? "click = resume   r = reset" : "up/down = minutes   click = start");
    }
    lv_obj_set_style_text_color(s_big, cd_rings && s_mode == COUNTDOWN ? T_DANGER_HI : T_ACCENT, 0);
}

static void watch(lv_timer_t *)
{
    if (cd_running && cd_remaining() == 0) {
        cd_running = false;
        cd_left = 0;
        cd_rings = 6;
        cd_last_ring = 0;
        screen_wake();
        if (!s_root) ui_toast("Timer done");
    }
    if (cd_rings && millis() - cd_last_ring > 1000) {
        cd_last_ring = millis();
        cd_rings--;
        for (int i = 0; i < 3; i++) { audio_beep(2000, 90); delay(60); }
    }
    refresh();
}

static void ensure_watch()
{
    if (!s_watch) s_watch = lv_timer_create(watch, 100, nullptr);
}

static bool keys(uint32_t k)
{
    if (cd_rings) { cd_rings = 0; refresh(); return true; }
    if (k == LV_KEY_LEFT || k == LV_KEY_RIGHT) { s_mode = s_mode == STOPWATCH ? COUNTDOWN : STOPWATCH; refresh(); return true; }
    if (s_mode == STOPWATCH) {
        if (k == LV_KEY_ENTER) {
            if (sw_running) { sw_acc += millis() - sw_start; sw_running = false; }
            else { sw_start = millis(); sw_running = true; }
        } else if (k == 'l' && sw_running) {
            if (sw_lap_count == 4) { memmove(sw_laps, sw_laps + 1, 3 * sizeof(sw_laps[0])); sw_lap_count = 3; }
            sw_laps[sw_lap_count++] = sw_elapsed();
        } else if (k == 'r' && !sw_running) {
            sw_acc = 0;
            sw_lap_count = 0;
        } else return false;
        refresh();
        return true;
    }
    if (k == LV_KEY_ENTER) {
        if (cd_running) { cd_left = cd_remaining(); cd_running = false; }
        else if (cd_left) { cd_end = millis() + cd_left; cd_running = true; }
    } else if (k == 'r' && !cd_running) {
        cd_left = cd_set;
    } else if ((k == LV_KEY_PREV || k == LV_KEY_NEXT || k == '+' || k == '-') && !cd_running) {
        bool up = k == LV_KEY_PREV || k == '+';
        // Below a minute, step by 10 s so short timers are reachable
        uint32_t step = (up ? cd_set >= 60000 : cd_set > 60000) ? 60000 : 10000;
        if (up) cd_set = cd_set + step > 24 * 3600000UL ? cd_set : cd_set + step;
        else cd_set = cd_set > step ? cd_set - step : 10000;
        cd_left = cd_set;
    } else return false;
    refresh();
    return true;
}

static lv_obj_t *tab(lv_obj_t *parent, const char *text)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 150, 26);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 2, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

static void open(lv_obj_t *parent)
{
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_t *row = lv_obj_create(s_root);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 26);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_tabs[0] = tab(row, LV_SYMBOL_PLAY " Stopwatch");
    s_tabs[1] = tab(row, LV_SYMBOL_BELL " Timer");
    s_big = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_big, &lv_font_montserrat_28, 0);
    lv_obj_align(s_big, LV_ALIGN_TOP_MID, 0, 50);
    s_sub = lv_label_create(s_root);
    lv_obj_set_style_text_color(s_sub, T_DIM, 0);
    lv_obj_align(s_sub, LV_ALIGN_TOP_MID, 0, 96);
    s_hint = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_hint, T_DIM, 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -4);
    ui_set_app_key_hook(keys);
    ensure_watch();
    refresh();
}

static void close() { s_root = nullptr; }

const App app_timer = {"Timer", LV_SYMBOL_BELL, open, close, nullptr, nullptr};
