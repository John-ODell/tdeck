#include "storage.h"
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "board.h"

static bool s_ok = false;

bool sd_mount()
{
    if (s_ok) return true;
    digitalWrite(BOARD_SDCARD_CS, HIGH);
    digitalWrite(RADIO_CS_PIN, HIGH);
    digitalWrite(BOARD_TFT_CS, HIGH);
    // LilyGO uses 800 kHz; try faster first, fall back for marginal cards
    s_ok = SD.begin(BOARD_SDCARD_CS, SPI, 10000000, "/sd") || SD.begin(BOARD_SDCARD_CS, SPI, 800000, "/sd");
    if (s_ok && SD.cardType() == CARD_NONE) { SD.end(); s_ok = false; }
    return s_ok;
}

bool sd_ok() { return s_ok; }
uint64_t sd_total_bytes() { return s_ok ? SD.totalBytes() : 0; }
uint64_t sd_used_bytes() { return s_ok ? SD.usedBytes() : 0; }
