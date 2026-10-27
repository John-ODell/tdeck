// T-Deck Pro bring-up (OS milestone 0). Built only by env:tdeck-pro until the Pro has
// its own display and input drivers. It runs the board layer, prints the banner, checks
// that every part answers, and prints a pass/fail line. Send 'b' over serial to repeat.
//
// After the part checks it runs the display test (OS milestone 1): see display_test().
// Nothing is transmitted on the radio and the modem stays off.
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <lvgl.h>
#include "board.h"
#include "hal/display.h"
#include "hal/epd_task.h"
#include "hal/epd_uc8253.h"

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
    display_init();
    lv_obj_t *scr = lv_scr_act();
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
}

void setup()
{
    Serial.begin(115200);
    uint32_t t = millis();
    while (!Serial && millis() - t < 3000) delay(10);
    board_early_init();
    run_checks();
    display_test();
}

void loop()
{
    if (Serial.available() && Serial.read() == 'b') run_checks();
    lv_timer_handler();
    delay(5);
}
