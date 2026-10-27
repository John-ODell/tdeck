#include "hal/touch_cst66xx.h"
#include <Arduino.h>
#include <Wire.h>
#include "board.h"

namespace cst {

static const uint8_t ADDR = 0x1A;
static volatile uint32_t s_edges;
static volatile bool s_edge;
static Stats s_stats;

static void IRAM_ATTR on_int() { s_edges++; s_edge = true; }

// Write a 32-bit register address (big-endian), then optionally read rlen bytes.
static bool wr_reg(uint32_t reg, uint8_t *rbuf = nullptr, size_t rlen = 0)
{
    Wire.beginTransmission(ADDR);
    uint8_t a[4] = {(uint8_t)(reg >> 24), (uint8_t)(reg >> 16), (uint8_t)(reg >> 8), (uint8_t)reg};
    Wire.write(a, 4);
    if (Wire.endTransmission() != 0) { s_stats.i2c_errors++; return false; }
    if (!rlen) return true;
    if (Wire.requestFrom((int)ADDR, (int)rlen) != (int)rlen) { s_stats.i2c_errors++; return false; }
    for (size_t i = 0; i < rlen; i++) rbuf[i] = Wire.read();
    return true;
}

static bool read_more(uint8_t *rbuf, size_t rlen)
{
    if (Wire.requestFrom((int)ADDR, (int)rlen) != (int)rlen) { s_stats.i2c_errors++; return false; }
    for (size_t i = 0; i < rlen; i++) rbuf[i] = Wire.read();
    return true;
}

static uint16_t sum16(uint16_t seed, const uint8_t *p, size_t n)
{
    while (n--) seed += *p++;
    return seed;
}

static bool normal_mode()
{
    bool ok = wr_reg(0xD0000400);  // disable low-power I2C, twice as the vendor does
    delay(1);
    ok = wr_reg(0xD0000400);
    ok &= wr_reg(0xD0000000);
    ok &= wr_reg(0xD0000C00);
    ok &= wr_reg(0xD0000100);
    return ok;
}

bool init(Info *info)
{
    Info in;
    uint8_t b[50];
    for (int attempt = 0; attempt < 4 && !in.ok; attempt++) {
        normal_mode();
        if (wr_reg(0xD0030000, b, sizeof b) && b[2] == 0xCA && b[3] == 0xCA) {
            in.ok = true;
        } else {
            delay(1);
            wr_reg(0xD0000400);
        }
    }
    if (in.ok) {
        in.chip_type = (uint32_t)b[3] << 24 | (uint32_t)b[2] << 16 | (uint32_t)b[1] << 8 | b[0];
        in.fw_ver = (uint32_t)b[35] << 24 | (uint32_t)b[34] << 16 | (uint32_t)b[33] << 8 | b[32];
        in.project = (uint32_t)b[39] << 24 | (uint32_t)b[38] << 16 | (uint32_t)b[37] << 8 | b[36];
        in.res_x = (uint16_t)(b[29] << 8 | b[28]);
        in.res_y = (uint16_t)(b[31] << 8 | b[30]);
        in.tx = b[48];
        in.rx = b[49];
        in.keys = b[27];
        normal_mode();
        pinMode(BOARD_TOUCH_INT, INPUT_PULLUP);
        attachInterrupt(BOARD_TOUCH_INT, on_int, FALLING);
    }
    if (info) *info = in;
    return in.ok;
}

bool pending() { return s_edge; }

bool read(bool *down, int16_t *x, int16_t *y)
{
    s_edge = false;
    uint8_t b[9 + 5 * 4];
    for (int retry = 0; retry < 2; retry++) {
        if (!wr_reg(0xD0070000, b, 9)) continue;
        uint8_t fingers = b[3] & 0x0F, keys = b[3] >> 4;
        uint8_t total = fingers + keys;
        if (total > 5) continue;
        if (total > 1 && !read_more(b + 9, (total - 1) * 5)) continue;
        if (sum16(0x55, b + 4, total * 5) != (uint16_t)(b[0] | b[1] << 8)) {
            s_stats.checksum_errors++;
            continue;
        }
        wr_reg(0xD00002AB);  // acknowledge
        s_stats.reports++;
        *down = false;
        if (b[2] == 0xFF && fingers) {
            const uint8_t *p = b + keys * 5;
            if (p[8] >> 4) {  // event: non-zero while the finger is down
                *down = true;
                *x = p[4] + ((p[7] & 0x0F) << 8);
                *y = p[5] + ((p[7] & 0xF0) << 4);
            }
        }
        return true;
    }
    wr_reg(0xD00002AB);
    return false;
}

void sleep()
{
    wr_reg(0xD0000400);
    delay(1);
    wr_reg(0xD0000400);
    wr_reg(0xD00022AB);
}

Stats stats()
{
    Stats s = s_stats;
    s.edges = s_edges;
    return s;
}

}  // namespace cst
