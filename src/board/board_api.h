// Board layer: bring-up, identity, capabilities and the shared SPI bus lock.
// Implemented in src/board/board.cpp for the board picked at compile time.
#pragma once
#include <stdint.h>
#include "board/caps.h"

class Print;

struct LoraPins {
    int8_t cs, dio1, rst, busy;
    int8_t power_en;        // -1 when the radio has no enable pin
    float tcxo_v;           // TCXO supply on DIO3
    bool dio2_rf_switch;
};

// Run first in setup(): releases sleep holds, sets every rail to its default, parks the
// SPI chip selects, starts SPI and I2C, and on the Pro works out the revision before any
// revision-specific pin is driven.
void board_early_init();

BoardModel board_model();
BoardRev board_rev();
const char *board_name();
uint32_t board_caps();
inline bool board_has(uint32_t caps) { return (board_caps() & caps) == caps; }

const LoraPins &board_lora_pins();

// The one SPI bus is shared by the display, the SD card and the radio. Recursive, so a
// driver can hold it across a transaction that calls other locked helpers.
bool board_spi_lock(uint32_t timeout_ms = 0xFFFFFFFFu);
void board_spi_unlock();

// Model, revision, capabilities, memory and an I2C scan with part names.
void board_print_banner(Print &out);
