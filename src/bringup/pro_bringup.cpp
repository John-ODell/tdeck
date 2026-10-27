// T-Deck Pro bring-up (OS milestone 0). Built only by env:tdeck-pro until the Pro has
// its own display and input drivers. It runs the board layer, prints the banner, checks
// that every part answers, and prints a pass/fail line. Send 'b' over serial to repeat.
//
// After the part checks it checks the keyboard and touch chips (OS milestone 2), then
// leaves a typing and tapping screen up for checking every key and row by hand. Serial
// commands: 'b' repeats the part checks, 'd' runs the display test (OS milestone 1,
// about 2.5 minutes), 's' prints the input counters.
// Nothing is transmitted on the radio and the modem stays off.
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <lvgl.h>
#include "board.h"
#include "hal/display.h"
#include "hal/epd_task.h"
#include "hal/epd_uc8253.h"
#include "hal/input.h"
#include "hal/input_pro.h"
#include "hal/tca8418.h"

static bool sx1262_version(char *ver, size_t n)
{
    if (!board_spi_lock(1000)) return false;
    digitalWrite(RADIO_RST_PIN, LOW); delay(2); digitalWrite(RADIO_RST_PIN, HIGH); delay(20);
    uint32_t t = millis();
    while (digitalRead(RADIO_BUSY_PIN) && millis() - t < 200) delay(1);
    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(RADIO_CS_PIN, LOW);
    SPI.transfer(0x1D); SPI.transfer(0x03); SPI.transfer(0x20); SPI.transfer(0x00);  // ReadRegister 0x0320
    for (size_t k = 0; k + 1 < n && k < 16; k++) {
        char c = (char)SPI.transfer(0x00);
        ver[k] = (c < 32 || c > 126) ? '.' : c;
        ver[k + 1] = 0;
    }
    digitalWrite(RADIO_CS_PIN, HIGH);
    SPI.endTransaction();
    board_spi_unlock();
    return strncmp(ver, "SX126", 5) == 0;
}

// Powers the GPS for a few seconds, looks for NMEA at 38400, then powers it off again.
static bool gps_answers()
{
    digitalWrite(BOARD_GPS_EN, HIGH);
    Serial2.begin(BOARD_GPS_BAUD, SERIAL_8N1, BOARD_GPS_RX_PIN, BOARD_GPS_TX_PIN);
    uint32_t t = millis();
    bool seen = false;
    char prev = 0;
    while (!seen && millis() - t < 4000) {
        while (Serial2.available()) {
            char c = (char)Serial2.read();
            if (prev == '$' && c == 'G') seen = true;
            prev = c;
        }
        delay(5);
    }
    Serial2.end();
    digitalWrite(BOARD_GPS_EN, LOW);
    return seen;
}

static bool i2c_present(uint8_t a)
{
    Wire.beginTransmission(a);
    return Wire.endTransmission() == 0;
}

static void check(const char *name, bool ok, int &fails)
{
    Serial.printf("[CHECK] %-22s %s\n", name, ok ? "ok" : "MISSING");
    if (!ok) fails++;
}

static void run_checks()
{
    board_print_banner(Serial);
    int fails = 0;
    check("revision known", board_rev() != BOARD_REV_PRO_UNKNOWN, fails);
    check("PSRAM", ESP.getPsramSize() > 0, fails);
    check("touch 0x1A", i2c_present(0x1A), fails);
    check("IMU 0x28", i2c_present(0x28), fails);
    check("keyboard 0x34", i2c_present(KEYBOARD_I2C_ADDR), fails);
    check("fuel gauge 0x55", i2c_present(0x55), fails);
    check("charger 0x6B", i2c_present(0x6B), fails);
    char ver[17] = "";
    bool radio = sx1262_version(ver, sizeof ver);
    Serial.printf("[CHECK] radio version \"%s\"\n", ver);
    check("SX1262", radio, fails);
    check("GPS NMEA at 38400", gps_answers(), fails);
    Serial.println("[CHECK] modem                  off by default (not powered)");
    Serial.printf("[BRINGUP] %s (%d missing)\n", fails ? "FAIL" : "PASS", fails);
}

// ---------- display test ----------
//
// Stands in for the main loop: polls "input" every millisecond and runs LVGL, and records
// the longest gap between iterations. The display task must never make that gap long.

static uint32_t s_last_iter_us, s_max_gap_us;
static lv_obj_t *s_body, *s_status;

static void pump(uint32_t ms)
{
    uint32_t end = millis() + ms;
    while ((int32_t)(end - millis()) > 0) {
        uint32_t n = micros();
        if (s_last_iter_us && n - s_last_iter_us > s_max_gap_us) s_max_gap_us = n - s_last_iter_us;
        s_last_iter_us = n;
        input_poll();
        lv_timer_handler();
        delay(1);
    }
}

static bool pump_until_idle(uint32_t timeout_ms)
{
    uint32_t t = millis();
    pump(20);
    while (!epd::idle()) {
        if (millis() - t > timeout_ms) return false;
        pump(5);
    }
    return true;
}

static void display_test()
{
    Serial.println("[EPD] display test: slow full, single changes, typing burst, 90 s idle");
    lv_obj_t *prev = lv_scr_act();
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_scr_load(scr);
    // The shared lv_conf.h uses the Plus's dark theme; the mono theme is milestone 3.
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_text_color(scr, lv_color_black(), 0);
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "T-Deck Pro bring-up");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
    s_status = lv_label_create(scr);
    lv_label_set_text(s_status, "display test");
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -8);
    s_body = lv_label_create(scr);
    lv_obj_set_width(s_body, SCREEN_W - 20);
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_body, "");
    lv_obj_align(s_body, LV_ALIGN_TOP_LEFT, 10, 40);

    lv_obj_update_layout(scr);  // settle label sizes so the first render is the final one
    bool ok = pump_until_idle(10000);
    epd::Stats s = epd::stats();
    Serial.printf("[EPD] first frame: %u slow full in %u ms, %u partials after it%s\n", s.slow_fulls,
                  s.last_slow_full_ms, s.partials, ok ? "" : " TIMEOUT");
    bool first_ok = ok && s.slow_fulls == 1 && s.partials == 0;
    if (s.partials)
        Serial.printf("[EPD] first frame: extra partial drew x %d..%d y %d..%d, %u ms after boot\n", s.last_rect.x0,
                      s.last_rect.x1, s.last_rect.y0, s.last_rect.y1, s.last_end_ms);

    // Single changes on an idle panel
    epd::reset_stats();
    uint32_t max_single = 0, singles_drawn = 0;
    for (int i = 1; i <= 5; i++) {
        char buf[32];
        snprintf(buf, sizeof buf, "single change %d", i);
        lv_label_set_text(s_body, buf);
        pump_until_idle(5000);
        s = epd::stats();
        Serial.printf("[EPD] single %d: start after %u ms, refresh %u ms (busy %u ms)\n", i,
                      s.last_start_latency_ms, s.last_total_ms, s.last_busy_ms);
        if (s.last_total_ms > max_single) max_single = s.last_total_ms;
        singles_drawn = s.partials + s.fast_fulls;
        pump(1000);
    }

    // Typing burst: 40 characters at 120 ms (about 100 words a minute)
    epd::reset_stats();
    s_last_iter_us = 0;
    s_max_gap_us = 0;
    static char text[64] = "";
    const char *src = "the quick brown fox jumps over the lazy";
    uint32_t t0 = millis();
    for (int i = 0; src[i]; i++) {
        text[i] = src[i];
        text[i + 1] = 0;
        lv_label_set_text(s_body, text);
        pump(120);
    }
    uint32_t typed = millis() - t0;
    pump_until_idle(10000);
    uint32_t settled = millis() - t0 - typed;
    s = epd::stats();
    uint32_t burst_gap = s_max_gap_us;
    uint32_t burst_drawn = s.partials + s.fast_fulls;
    uint32_t worst_partial = s.max_partial_ms > max_single ? s.max_partial_ms : max_single;
    Serial.printf("[EPD] burst: %u keys in %u ms, %u partials, %u fast fulls, glass settled %u ms after the last key\n",
                  (unsigned)strlen(src), typed, s.partials, s.fast_fulls, settled);
    Serial.printf("[EPD] burst: longest partial %u ms, longest input-loop gap %.1f ms\n", s.max_partial_ms,
                  burst_gap / 1000.0);

    // Idle: redraw the same text every second for 90 s; nothing should reach the glass
    epd::reset_stats();
    s_last_iter_us = 0;
    s_max_gap_us = 0;
    for (int i = 0; i < 90; i++) {
        lv_label_set_text(s_body, text);
        lv_label_set_text(s_status, "display test");
        pump(1000);
    }
    s = epd::stats();
    uint32_t idle_refreshes = s.partials + s.fast_fulls + s.slow_fulls;
    Serial.printf("[EPD] idle 90 s: %u refreshes, %u skipped as unchanged, longest input-loop gap %.1f ms\n",
                  idle_refreshes, s.skipped, s_max_gap_us / 1000.0);

    epd::reset_stats();
    lv_label_set_text(s_body, "display test done");
    epd::request_full(epd::FULL_SLOW);
    pump_until_idle(10000);
    pump(2000);  // idle power-off
    s = epd::stats();
    bool final_ok = s.slow_fulls == 1 && s.partials == 0;
    Serial.printf("[EPD] final: %u slow full in %u ms, %u partials; panel powered %s; refused commands %u\n",
                  s.slow_fulls, s.last_slow_full_ms, s.partials,
                  epd::powered() ? "yes" : "no", epd::refused_commands());

    bool pass = first_ok && final_ok && singles_drawn == 5 && burst_drawn > 0 && worst_partial <= 680 && burst_gap <= 20000 && idle_refreshes <= 1 &&
                epd::refused_commands() == 0;
    Serial.printf("[EPD] limits: 5 single changes drawn (%u), burst drawn (%u), partial <= 680 ms (worst %u), "
                  "input gap <= 20 ms (%.1f), idle refreshes <= 1 (%u)\n",
                  singles_drawn, burst_drawn, worst_partial, burst_gap / 1000.0, idle_refreshes);
    Serial.printf("[EPD] %s\n", pass ? "PASS" : "FAIL");
    lv_scr_load(prev);
    lv_obj_del(scr);
}

// ---------- input check (milestone 2) ----------

static lv_obj_t *s_in_status, *s_ta;
static int s_last_row = -1;

static void style_mono(lv_obj_t *scr)
{
    // The shared lv_conf.h uses the Plus's dark theme; the mono theme is milestone 3.
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_text_color(scr, lv_color_black(), 0);
}

static void log_key(uint8_t code, uint32_t key)
{
    if (key >= ' ' && key < 127) Serial.printf("[KEY] code %2u -> '%c'\n", code, (char)key);
    else Serial.printf("[KEY] code %2u -> key %u\n", code, (unsigned)key);
}

static void on_row(lv_event_t *e)
{
    int row = (int)(intptr_t)lv_event_get_user_data(e);
    lv_point_t p = {0, 0};
    lv_indev_t *in = lv_indev_get_act();
    if (in) lv_indev_get_point(in, &p);
    s_last_row = row;
    Serial.printf("[TAP] row %d at x %d y %d\n", row, p.x, p.y);
}

static void on_lock(bool lock)
{
    input_set_locked(lock);
    Serial.printf("[LOCK] %s\n", lock ? "locked" : "unlocked");
}

static void print_input_stats()
{
    ProInputStats st = input_pro_stats();
    Serial.printf("[INPUT] keys %u delivered, %u dropped; TCA8418 %u events, %u overflows, %u INT edges, "
                  "longest wait %u ms\n", st.keys_delivered, st.keys_dropped, st.kb_events, st.kb_overflows,
                  st.kb_edges, st.max_kb_wait_ms);
    Serial.printf("[INPUT] touch %u reports, %u checksum errors, %u I2C errors, %u INT edges; blocked %u\n",
                  st.touch_reports, st.touch_checksum_errors, st.touch_i2c_errors, st.touch_edges,
                  input_blocked_count());
}

static void input_checks()
{
    int fails = 0;
    int cfg = tca::read_reg(0x01);
    check("TCA8418 interrupt on", input_keyboard_found() && cfg == 0x89, fails);
    check("TCA8418 matrix 4x10", tca::read_reg(0x1D) == 0x0F && tca::read_reg(0x1E) == 0xFF &&
                                     tca::read_reg(0x1F) == 0x03, fails);
    check("keyboard INT idle high", digitalRead(BOARD_KEYBOARD_INT) == HIGH, fails);
    const cst::Info &ti = input_pro_touch_info();
    Serial.printf("[CHECK] touch IC type 0x%08X fw 0x%08X project 0x%08X res %ux%u tx %u rx %u keys %u\n",
                  ti.chip_type, ti.fw_ver, ti.project, ti.res_x, ti.res_y, ti.tx, ti.rx, ti.keys);
    check("touch info signature", input_touch_found(), fails);
    uint32_t e0 = input_pro_stats().touch_edges, k0 = input_pro_stats().kb_edges;
    pump(2000);
    check("no touch INT while idle", input_pro_stats().touch_edges == e0, fails);
    check("no key INT while idle", input_pro_stats().kb_edges == k0, fails);
    Serial.printf("[INPUT] checks %s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
}

static void input_screen()
{
    lv_obj_t *scr = lv_scr_act();
    style_mono(scr);
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Type, then tap each row");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    lv_group_t *g = lv_group_create();
    s_ta = lv_textarea_create(scr);
    lv_obj_set_size(s_ta, SCREEN_W - 8, 84);
    lv_obj_align(s_ta, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_set_style_bg_color(s_ta, lv_color_white(), 0);
    lv_obj_set_style_text_color(s_ta, lv_color_black(), 0);
    lv_obj_set_style_border_color(s_ta, lv_color_black(), 0);
    lv_obj_set_style_anim_time(s_ta, 0, LV_PART_CURSOR);  // no blinking cursor on e-paper
    lv_group_add_obj(g, s_ta);

    for (int i = 0; i < 6; i++) {
        lv_obj_t *b = lv_btn_create(scr);
        lv_obj_set_size(b, SCREEN_W - 8, 28);
        lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 114 + i * 32);
        lv_obj_set_style_bg_color(b, lv_color_white(), 0);
        lv_obj_set_style_border_color(b, lv_color_black(), 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text_fmt(l, "Row %d", i + 1);
        lv_obj_set_style_text_color(l, lv_color_black(), 0);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, on_row, LV_EVENT_CLICKED, (void *)(intptr_t)(i + 1));
    }
    s_in_status = lv_label_create(scr);
    lv_obj_align(s_in_status, LV_ALIGN_BOTTOM_LEFT, 4, -2);
    lv_label_set_text(s_in_status, "");

    input_set_group(g);
    lv_group_focus_obj(s_ta);
    input_pro_set_key_log(log_key);
    input_set_lock_hook(on_lock);
}

static void update_status()
{
    static char last[64] = "";
    const kb::State &m = input_pro_mods();
    char buf[64];
    snprintf(buf, sizeof buf, "%s %s %s  keys %u  lost %u  row %d%s", kb::mod_label(m, kb::MOD_SHIFT),
             kb::mod_label(m, kb::MOD_SYM), kb::mod_label(m, kb::MOD_ALT), input_pro_stats().keys_delivered,
             input_pro_stats().kb_overflows + input_pro_stats().keys_dropped, s_last_row,
             input_locked() ? "  LOCKED" : "");
    if (strcmp(buf, last)) {
        strcpy(last, buf);
        lv_label_set_text(s_in_status, buf);
    }
}

void setup()
{
    Serial.begin(115200);
    uint32_t t = millis();
    while (!Serial && millis() - t < 3000) delay(10);
    board_early_init();
    run_checks();
    display_init();
    input_init();
    input_screen();
    input_checks();
}

void loop()
{
    if (Serial.available()) {
        int c = Serial.read();
        if (c == 'b') run_checks();
        if (c == 'd') display_test();
        if (c == 's') print_input_stats();
    }
    uint32_t n = micros();
    if (s_last_iter_us && n - s_last_iter_us > s_max_gap_us) s_max_gap_us = n - s_last_iter_us;
    s_last_iter_us = n;
    input_poll();
    static uint32_t last_status = 0;
    if (millis() - last_status > 250) { last_status = millis(); update_status(); }
    lv_timer_handler();
    delay(1);
}
