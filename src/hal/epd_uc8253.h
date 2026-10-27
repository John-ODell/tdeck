// UltraChip UC8253 driver for the T-Deck Pro's GDEQ031T10 panel (240x320, 1 bit).
// Factory OTP waveforms only: no LUT registers are written. Every command goes through
// an allow list, so the OTP-programming commands (0xA0, 0xA1, 0xA3) can never be sent.
// Measured on a V1.1 unit (lilygo-A7682E bench/refresh_timing): 645 ms waveform floor.
#pragma once
#include <stdint.h>
#include "hal/epd_sched.h"

namespace epd {

struct Timing {
    uint32_t write_us = 0;    // both RAM planes
    uint32_t pon_us = 0;      // 0x04 power-on BUSY, 0 when already on
    uint32_t busy_us = 0;     // 0x12 refresh BUSY (the waveform)
    uint32_t total_us = 0;
};

bool hw_init(uint32_t spi_hz = 10000000);

// Draw `rect` of `frame` (full-frame buffer) with `mode`, then keep the controller's old
// plane equal to `frame`. A full mode always draws the whole screen. Blocks the calling
// task, but holds the SPI bus lock only while talking to the controller, never while BUSY.
bool refresh(const uint8_t *frame, Rect rect, Mode mode, Timing *t = nullptr);

// Set the controller's old plane without drawing (used once, before the first refresh).
void set_old_plane(const uint8_t *frame);

void power_off();        // 0x02; the image stays. About 40 ms.
bool powered();

// Commands refused by the allow list since boot (should stay 0).
uint32_t refused_commands();

}  // namespace epd
