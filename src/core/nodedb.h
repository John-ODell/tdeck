#pragma once
#include <stdint.h>
#include <stddef.h>

struct Node {
    uint32_t num;
    char long_name[40];
    char short_name[5];
    uint8_t hw_model;
    uint8_t role;
    bool has_user;
    bool has_key;
    uint8_t pub_key[32];
    bool has_pos;
    int32_t lat_i, lon_i, alt;
    uint32_t pos_time;
    uint32_t last_heard;     // epoch seconds if clock valid, else uptime seconds
    float snr;
    int16_t rssi;
    uint8_t hops_away;       // 0 = direct, 0xFF = unknown
    uint8_t battery;         // 0 = unknown, 101 = powered
    float voltage;
};

namespace nodedb {

static const size_t MAX_NODES = 120;

void load();
void save_if_dirty();       // debounced flash write
void mark_dirty();
void save_now();
Node *find(uint32_t num);
Node *get_or_create(uint32_t num);
size_t count();
Node *at(size_t i);         // sorted most-recent first after sort()
void sort_by_last_heard();
uint32_t version();         // bumps on every change, for UI refresh
const char *display_name(uint32_t num, char *buf, size_t cap);

// Breadcrumbs (#147): the last positions of a node, in RAM only, oldest first. A point is
// added when it is about 25 m or more from the previous one.
struct TrailPoint { int32_t lat_i, lon_i; };
static const size_t TRAIL_LEN = 16;
void trail_add(uint32_t num, int32_t lat_i, int32_t lon_i);
size_t trail(uint32_t num, TrailPoint *out, size_t max); // points copied, oldest first

} // namespace nodedb
