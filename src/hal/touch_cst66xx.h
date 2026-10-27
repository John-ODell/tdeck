// Hynitron CST66xx (CST3530 family) touch on the T-Deck Pro V1.1: I2C 0x1A, INT on GPIO12,
// reset on GPIO38 (pulsed by board_early_init). Protocol from LilyGo's factory driver
// (examples/factory/hyn_cst66xx.c, commit 4028fdcef8): 32-bit register addresses, a
// 16-bit sum seeded with 0x55 over each report, and an acknowledge after every read.
#pragma once
#include <stdint.h>

namespace cst {

struct Info {
    bool ok = false;
    uint32_t chip_type = 0, fw_ver = 0, project = 0;
    uint16_t res_x = 0, res_y = 0;
    uint8_t tx = 0, rx = 0, keys = 0;
};

bool init(Info *info);
bool pending();                         // INT edge since the last read
// Reads one report. down = a finger is on the panel; x, y in panel pixels.
bool read(bool *down, int16_t *x, int16_t *y);
void sleep();                           // deep sleep; needs a reset to wake

struct Stats {
    uint32_t reports = 0, checksum_errors = 0, i2c_errors = 0, edges = 0;
};
Stats stats();

}  // namespace cst
