#pragma once
#include <stdint.h>
#include <stddef.h>

// "now", "45s", "12m", "3h", "2d", or "?" for a Node::last_heard style stamp
const char *fmt_age(uint32_t stamp, char *buf, size_t cap);
// Great-circle distance (m) and initial bearing (deg) between 1e-7 degree points
double geo_distance_m(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2);
double geo_bearing_deg(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2);
const char *fmt_distance(double m, char *buf, size_t cap);   // miles/feet
const char *compass(double deg);
