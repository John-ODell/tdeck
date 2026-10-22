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

// Baud probe runs from gps_loop(): listen at each rate until valid NMEA arrives.
static const uint32_t PROBE_BAUD[] = {9600, 38400, 115200};
static const uint32_t PROBE_MS[] = {1500, 1500, 1000};
static int s_probe = 0;            // index into PROBE_BAUD, -1 when done
static uint32_t s_probe_start = 0, s_probe_passed = 0;

static void probe_start(int i)
{
    s_probe = i;
    port.updateBaudRate(PROBE_BAUD[i]);
    while (port.available()) port.read();
    s_probe_start = millis();
    s_probe_passed = gps.passedChecksum();
}

static void probe_found(int i)
{
    s_probe = -1;
    s.baud = PROBE_BAUD[i];
    if (i == 0) {
        s.model = "L76K";
        port.write("$PCAS04,5*1C\r\n"); // GPS + GLONASS
        port.write("$PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0*02\r\n"); // GGA + RMC only
        port.write("$PCAS11,3*1E\r\n"); // portable mode
    } else {
        s.model = i == 1 ? "u-blox" : "unknown";
    }
}

static void probe_step()
{
    if (gps.passedChecksum() > s_probe_passed + 1) { probe_found(s_probe); return; }
    if (millis() - s_probe_start < PROBE_MS[s_probe]) return;
    if (s_probe + 1 < 3) { probe_start(s_probe + 1); return; }
    // Nothing yet: a cold module can be silent for a while, keep listening at 9600
    s_probe = -1;
    port.updateBaudRate(9600);
    s.baud = 9600;
}

void gps_init()
{
    port.setRxBufferSize(1024);
    port.begin(9600, SERIAL_8N1, BOARD_GPS_RX_PIN, BOARD_GPS_TX_PIN);
    s.model = "none";
    probe_start(0);
}

void gps_loop()
{
    while (port.available()) gps.encode(port.read());
    if (s_probe >= 0) probe_step();
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
