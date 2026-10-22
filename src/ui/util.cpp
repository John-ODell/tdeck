#include "util.h"
#include <Arduino.h>
#include <math.h>
#include <time.h>

const char *fmt_age(uint32_t stamp, char *buf, size_t cap)
{
    time_t now = time(nullptr);
    int64_t age;
    if (stamp > 1700000000) {
        if (now < 1700000000) { strlcpy(buf, "?", cap); return buf; }
        age = (int64_t)now - stamp;
    } else {
        age = (int64_t)(millis() / 1000) - stamp;
    }
    if (age < 0) strlcpy(buf, "?", cap);
    else if (age < 30) strlcpy(buf, "now", cap);
    else if (age < 60) snprintf(buf, cap, "%ds", (int)age);
    else if (age < 3600) snprintf(buf, cap, "%dm", (int)(age / 60));
    else if (age < 86400) snprintf(buf, cap, "%dh", (int)(age / 3600));
    else snprintf(buf, cap, "%dd", (int)(age / 86400));
    return buf;
}

static double rad(int32_t e7) { return e7 * 1e-7 * M_PI / 180.0; }

double geo_distance_m(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2)
{
    double p1 = rad(lat1), p2 = rad(lat2), dp = p2 - p1, dl = rad(lon2) - rad(lon1);
    double a = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
    return 6371000.0 * 2 * atan2(sqrt(a), sqrt(1 - a));
}

double geo_bearing_deg(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2)
{
    double p1 = rad(lat1), p2 = rad(lat2), dl = rad(lon2) - rad(lon1);
    double y = sin(dl) * cos(p2), x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
    double b = atan2(y, x) * 180.0 / M_PI;
    return b < 0 ? b + 360 : b;
}

const char *fmt_distance(double m, char *buf, size_t cap)
{
    double mi = m / 1609.344;
    if (mi < 0.1) snprintf(buf, cap, "%d ft", (int)(m * 3.28084));
    else if (mi < 10) snprintf(buf, cap, "%.1f mi", mi);
    else snprintf(buf, cap, "%d mi", (int)mi);
    return buf;
}

const char *compass(double deg)
{
    static const char *d[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return d[(int)((deg + 22.5) / 45.0) & 7];
}
