#pragma once
#include <stdint.h>

struct GpsState {
    bool present;          // NMEA seen
    const char *model;     // "L76K", "u-blox", "unknown"
    uint32_t baud;
    bool fix;
    double lat, lon, alt_m, speed_kmh, course_deg, hdop;
    uint8_t sats;
    uint32_t chars, fix_age_ms;
    int year, month, day, hour, minute, second;
};

void gps_init();
void gps_loop();
const GpsState &gps_state();
