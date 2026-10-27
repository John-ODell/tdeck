// Board identity and capabilities. Pure C++ with no Arduino headers, so the host
// tests can check the revision and capability rules (test/host/test_board.cpp).
#pragma once
#include <stdint.h>

enum BoardModel : uint8_t {
    BOARD_MODEL_TDECK_PLUS = 0,
    BOARD_MODEL_TDECK_PRO = 1,
};

// T-Deck Pro hardware revisions. V1.0 and V1.1 reuse GPIO16, 38 and 45 for
// different jobs, so nothing may drive those pins until the revision is known.
enum BoardRev : uint8_t {
    BOARD_REV_NA = 0,       // T-Deck Plus: one revision
    BOARD_REV_PRO_V1_0 = 1, // LTR-553 light sensor at 0x23, touch reset on GPIO45
    BOARD_REV_PRO_V1_1 = 2, // DRV2605L at 0x5A, e-paper reset GPIO16, touch reset GPIO38
    BOARD_REV_PRO_UNKNOWN = 3,
};

// What the hardware has, not whether a driver for it exists yet.
enum BoardCap : uint32_t {
    CAP_COLOR_LCD   = 1u << 0,
    CAP_EPAPER      = 1u << 1,
    CAP_BACKLIGHT   = 1u << 2,
    CAP_TRACKBALL   = 1u << 3,
    CAP_TOUCH       = 1u << 4,
    CAP_I2S_SPEAKER = 1u << 5,
    CAP_HAPTICS     = 1u << 6,
    CAP_FUEL_GAUGE  = 1u << 7,  // BQ27220
    CAP_CHARGER_I2C = 1u << 8,  // BQ25896
    CAP_IMU         = 1u << 9,  // BHI260AP
    CAP_MODEM       = 1u << 10, // A7682E (the 4G SKU)
    CAP_GPS         = 1u << 11,
    CAP_LORA        = 1u << 12,
    CAP_KEYBOARD    = 1u << 13,
    CAP_SD          = 1u << 14,
    CAP_BATTERY_ADC = 1u << 15, // battery read through a divider, no gauge
    CAP_LIGHT_SENSOR = 1u << 16, // LTR-553 (Pro V1.0)
};

// Pro revision from two I2C probes made before any revision-specific pin is driven.
// A V1.0 board answers at 0x23 (LTR-553); a V1.1 board answers at 0x5A (DRV2605L).
inline BoardRev board_rev_from_probe(bool has_ltr553_0x23, bool has_drv2605_0x5a)
{
    if (has_ltr553_0x23 && !has_drv2605_0x5a) return BOARD_REV_PRO_V1_0;
    if (has_drv2605_0x5a && !has_ltr553_0x23) return BOARD_REV_PRO_V1_1;
    return BOARD_REV_PRO_UNKNOWN;
}

inline uint32_t board_caps_for(BoardModel model, BoardRev rev)
{
    const uint32_t common = CAP_TOUCH | CAP_GPS | CAP_LORA | CAP_KEYBOARD | CAP_SD;
    if (model == BOARD_MODEL_TDECK_PLUS)
        return common | CAP_COLOR_LCD | CAP_BACKLIGHT | CAP_TRACKBALL | CAP_I2S_SPEAKER | CAP_BATTERY_ADC;
    uint32_t c = common | CAP_EPAPER | CAP_FUEL_GAUGE | CAP_CHARGER_I2C | CAP_IMU | CAP_MODEM;
    if (rev == BOARD_REV_PRO_V1_0) c |= CAP_LIGHT_SENSOR | CAP_HAPTICS;  // plain motor on GPIO2
    if (rev == BOARD_REV_PRO_V1_1) c |= CAP_HAPTICS;                     // DRV2605L
    return c;
}

inline const char *board_rev_name(BoardRev rev)
{
    switch (rev) {
    case BOARD_REV_PRO_V1_0: return "V1.0";
    case BOARD_REV_PRO_V1_1: return "V1.1";
    case BOARD_REV_PRO_UNKNOWN: return "unknown";
    default: return "-";
    }
}
