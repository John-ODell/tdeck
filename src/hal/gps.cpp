#include "gps.h"
#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <sys/time.h>
#include "board.h"
#include "mesh/mesh.h"
#include "core/settings.h"

static TinyGPSPlus gps;
static GpsState s = {};
static HardwareSerial &port = Serial1;

// Listen at a baud rate and report whether valid NMEA sentences arrive.
static bool probe(uint32_t baud, uint32_t ms)
{
    port.updateBaudRate(baud);
    while (port.available()) port.read();
    uint32_t passed = gps.passedChecksum(), start = millis();
    while (millis() - start < ms) {
        while (port.available()) gps.encode(port.read());
        if (gps.passedChecksum() > passed + 1) return true;
        delay(5);
    }
    return false;
}

void gps_init()
{
    port.setRxBufferSize(1024);
    port.begin(9600, SERIAL_8N1, BOARD_GPS_RX_PIN, BOARD_GPS_TX_PIN);
    s.model = "none";
    // L76K defaults to 9600; the u-blox M10 variant to 38400
    if (probe(9600, 1500)) {
        s.baud = 9600;
        s.model = "L76K";
        port.write("$PCAS04,5*1C\r\n"); // GPS + GLONASS
        delay(50);
        port.write("$PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0*02\r\n"); // GGA + RMC only
        delay(50);
        port.write("$PCAS11,3*1E\r\n"); // vehicle/portable mode
    } else if (probe(38400, 1500)) {
        s.baud = 38400;
        s.model = "u-blox";
    } else if (probe(115200, 1000)) {
        s.baud = 115200;
        s.model = "unknown";
    } else {
        // Keep listening at 9600: a cold module can be silent for a while
        port.updateBaudRate(9600);
        s.baud = 9600;
    }
}

void gps_loop()
{
    while (port.available()) gps.encode(port.read());
    s.chars = gps.charsProcessed();
    s.present = gps.passedChecksum() > 0;
    if (s.present && !strcmp(s.model, "none")) s.model = "unknown";
    s.sats = gps.satellites.isValid() ? gps.satellites.value() : 0;
    s.hdop = gps.hdop.isValid() ? gps.hdop.hdop() : 0;
    s.fix = gps.location.isValid() && gps.location.age() < 5000;
    if (gps.location.isValid()) {
        s.lat = gps.location.lat();
        s.lon = gps.location.lng();
        s.fix_age_ms = gps.location.age();
    }
    if (gps.altitude.isValid()) s.alt_m = gps.altitude.meters();
    if (gps.speed.isValid()) s.speed_kmh = gps.speed.kmph();
    if (gps.course.isValid()) s.course_deg = gps.course.deg();
    if (gps.date.isValid() && gps.time.isValid()) {
        s.year = gps.date.year(); s.month = gps.date.month(); s.day = gps.date.day();
        s.hour = gps.time.hour(); s.minute = gps.time.minute(); s.second = gps.time.second();
    }

    static uint32_t last_push = 0;
    if (millis() - last_push < 5000) return;
    last_push = millis();

    if (s.fix) mesh::set_own_position((int32_t)(s.lat * 1e7), (int32_t)(s.lon * 1e7), (int32_t)s.alt_m, s.sats);

    // Set the clock from GPS UTC once a minute while valid
    static uint32_t last_clock = 0;
    if (s.year >= 2024 && gps.time.age() < 2000 && (!last_clock || millis() - last_clock > 60000)) {
        last_clock = millis();
        struct tm t = {};
        t.tm_year = s.year - 1900; t.tm_mon = s.month - 1; t.tm_mday = s.day;
        t.tm_hour = s.hour; t.tm_min = s.minute; t.tm_sec = s.second;
        setenv("TZ", "UTC0", 1);
        tzset();
        time_t utc = mktime(&t);
        struct timeval tv = {utc, 0};
        settimeofday(&tv, nullptr);
        apply_timezone();
    }
}

const GpsState &gps_state() { return s; }
