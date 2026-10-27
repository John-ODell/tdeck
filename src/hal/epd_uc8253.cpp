#include "hal/epd_uc8253.h"
#include <Arduino.h>
#include <SPI.h>
#include <esp_timer.h>
#include "board.h"

namespace epd {

static uint32_t s_hz = 10000000;
static bool s_powered = false;
static uint32_t s_refused = 0;

// Everything this driver needs. Anything else, the OTP-programming commands above all,
// is refused here rather than trusted to every caller.
static bool allowed(uint8_t c)
{
    switch (c) {
    case 0x00:  // PSR panel setting
    case 0x02:  // POF power off
    case 0x04:  // PON power on
    case 0x10:  // DTM1 old plane
    case 0x12:  // DRF refresh
    case 0x13:  // DTM2 new plane
    case 0x50:  // CDI
    case 0x90:  // PTL partial window
    case 0x91:  // PTIN
    case 0x92:  // PTOUT
    case 0xE0:  // CCSET temperature source
    case 0xE5:  // TSSET forced temperature (selects the OTP waveform)
        return true;
    default:
        return false;
    }
}

static bool cmd(uint8_t c)
{
    if (!allowed(c)) {
        s_refused++;
        Serial.printf("epd: refused command 0x%02X\n", c);
        return false;
    }
    SPI.beginTransaction(SPISettings(s_hz, MSBFIRST, SPI_MODE0));
    digitalWrite(BOARD_EPD_DC, LOW);
    digitalWrite(BOARD_EPD_CS, LOW);
    SPI.transfer(c);
    digitalWrite(BOARD_EPD_CS, HIGH);
    digitalWrite(BOARD_EPD_DC, HIGH);
    SPI.endTransaction();
    return true;
}

static void data(const uint8_t *p, size_t n)
{
    SPI.beginTransaction(SPISettings(s_hz, MSBFIRST, SPI_MODE0));
    digitalWrite(BOARD_EPD_CS, LOW);
    SPI.writeBytes(p, n);
    digitalWrite(BOARD_EPD_CS, HIGH);
    SPI.endTransaction();
}

static void data1(uint8_t d) { data(&d, 1); }

// Waits for BUSY (LOW while busy) to clear without holding the bus. Returns microseconds.
static uint32_t wait_busy(uint32_t timeout_ms = 10000)
{
    int64_t t0 = esp_timer_get_time();
    delayMicroseconds(100);  // let BUSY assert
    while (digitalRead(BOARD_EPD_BUSY) == LOW) {
        if (esp_timer_get_time() - t0 > (int64_t)timeout_ms * 1000) {
            Serial.println("epd: BUSY timeout");
            break;
        }
        vTaskDelay(1);
    }
    return (uint32_t)(esp_timer_get_time() - t0);
}

static void window(const Rect &r)
{
    int xs = r.x0 & ~7, xe = r.x1 | 7;
    uint8_t w[7] = {(uint8_t)xs, (uint8_t)xe, (uint8_t)(r.y0 >> 8), (uint8_t)(r.y0 & 0xFF),
                    (uint8_t)(r.y1 >> 8), (uint8_t)(r.y1 & 0xFF), 0x01};  // PT_SCAN: no measured effect
    cmd(0x90);
    data(w, sizeof w);
}

static void write_plane(uint8_t ram, const uint8_t *frame, const Rect &r)
{
    cmd(0x91);
    window(r);
    cmd(ram);
    int bx0 = r.x0 / 8, bx1 = r.x1 / 8, bw = bx1 - bx0 + 1;
    if (bw == ROW_BYTES)
        data(frame + r.y0 * ROW_BYTES, (size_t)bw * (r.y1 - r.y0 + 1));
    else
        for (int y = r.y0; y <= r.y1; y++) data(frame + y * ROW_BYTES + bx0, bw);
    cmd(0x92);
}

bool hw_init(uint32_t spi_hz)
{
    s_hz = spi_hz;
    pinMode(BOARD_EPD_CS, OUTPUT);
    digitalWrite(BOARD_EPD_CS, HIGH);
    pinMode(BOARD_EPD_DC, OUTPUT);
    digitalWrite(BOARD_EPD_DC, HIGH);
    pinMode(BOARD_EPD_BUSY, INPUT);
    if (board_rev() != BOARD_REV_PRO_V1_1) return false;  // V1.0 has no panel reset line

    // board_early_init() pulsed the reset on GPIO16. OTP black/white mode, as GxEPD2.
    wait_busy(2000);
    if (!board_spi_lock(1000)) return false;
    cmd(0x00); data1(0x1E); data1(0x0D);  // soft reset
    delay(1);
    cmd(0x00); data1(0x1F); data1(0x0D);
    board_spi_unlock();
    s_powered = false;
    return true;
}

void set_old_plane(const uint8_t *frame)
{
    board_spi_lock();
    write_plane(0x10, frame, full_rect());
    board_spi_unlock();
}

bool refresh(const uint8_t *frame, Rect r, Mode mode, Timing *t)
{
    Timing tm;
    int64_t t0 = esp_timer_get_time();
    r = mode == MODE_PARTIAL ? clip(r) : full_rect();
    if (r.empty()) return false;
    r.x0 &= ~7;
    r.x1 |= 7;

    board_spi_lock();
    write_plane(0x13, frame, r);
    tm.write_us = (uint32_t)(esp_timer_get_time() - t0);
    cmd(0x91);
    window(r);
    if (mode == MODE_SLOW_FULL) {
        cmd(0xE0); data1(0x00);                   // real temperature: OTP TR0..TR4
    } else {
        cmd(0xE0); data1(0x02);
        cmd(0xE5); data1(mode == MODE_PARTIAL ? 0x79 : 0x5A);  // TR7 partial, TR5 fast full
    }
    cmd(0x50); data1(mode == MODE_PARTIAL ? 0xD7 : 0x97);
    if (!s_powered) {
        cmd(0x04);
        board_spi_unlock();
        tm.pon_us = wait_busy(1000);
        board_spi_lock();
        s_powered = true;
    }
    cmd(0x12);
    board_spi_unlock();
    tm.busy_us = wait_busy();
    board_spi_lock();
    cmd(0x92);
    int64_t tw = esp_timer_get_time();
    write_plane(0x10, frame, r);
    tm.write_us += (uint32_t)(esp_timer_get_time() - tw);
    board_spi_unlock();
    tm.total_us = (uint32_t)(esp_timer_get_time() - t0);
    if (t) *t = tm;
    return true;
}

void power_off()
{
    if (!s_powered) return;
    board_spi_lock();
    cmd(0x02);
    board_spi_unlock();
    wait_busy(1000);
    s_powered = false;
}

bool powered() { return s_powered; }
uint32_t refused_commands() { return s_refused; }

}  // namespace epd
