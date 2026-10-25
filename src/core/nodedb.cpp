#include "nodedb.h"
#include <math.h>
#include <Arduino.h>
#include <LittleFS.h>
#include <algorithm>

namespace nodedb {

static Node *s_nodes = nullptr;
static size_t s_count = 0;
static bool s_dirty = false;
static uint32_t s_dirty_at = 0, s_version = 1;
static const char *PATH = "/nodes.bin";
static const uint32_t MAGIC = 0x4E4F4431; // "NOD1"

void load()
{
    if (!s_nodes) s_nodes = (Node *)ps_calloc(MAX_NODES, sizeof(Node));
    if (!s_nodes) s_nodes = (Node *)calloc(MAX_NODES, sizeof(Node)); // no PSRAM
    s_count = 0;
    File f = LittleFS.open(PATH, "r");
    if (!f) return;
    uint32_t magic = 0, n = 0;
    f.read((uint8_t *)&magic, 4);
    f.read((uint8_t *)&n, 4);
    if (magic == MAGIC && n <= MAX_NODES && f.size() == 8 + n * sizeof(Node)) {
        f.read((uint8_t *)s_nodes, n * sizeof(Node));
        s_count = n;
    }
    f.close();
}

void mark_dirty()
{
    if (!s_dirty) s_dirty_at = millis();
    s_dirty = true;
    s_version++;
}

void save_if_dirty()
{
    if (!s_dirty || millis() - s_dirty_at < 30000) return;
    save_now();
}

void save_now()
{
    if (!s_dirty) return;
    s_dirty = false;
    File f = LittleFS.open(PATH, "w");
    if (!f) return;
    uint32_t n = s_count;
    f.write((const uint8_t *)&MAGIC, 4);
    f.write((const uint8_t *)&n, 4);
    f.write((const uint8_t *)s_nodes, n * sizeof(Node));
    f.close();
}

Node *find(uint32_t num)
{
    for (size_t i = 0; i < s_count; i++)
        if (s_nodes[i].num == num) return &s_nodes[i];
    return nullptr;
}

Node *get_or_create(uint32_t num)
{
    Node *n = find(num);
    if (n) return n;
    if (s_count == MAX_NODES) {
        // Evict the least recently heard node
        size_t oldest = 0;
        for (size_t i = 1; i < s_count; i++)
            if (s_nodes[i].last_heard < s_nodes[oldest].last_heard) oldest = i;
        s_nodes[oldest] = s_nodes[--s_count];
    }
    n = &s_nodes[s_count++];
    memset(n, 0, sizeof(Node));
    n->num = num;
    n->hops_away = 0xFF;
    snprintf(n->short_name, sizeof(n->short_name), "%04x", (unsigned)(num & 0xffff));
    mark_dirty();
    return n;
}

size_t count() { return s_count; }
Node *at(size_t i) { return i < s_count ? &s_nodes[i] : nullptr; }
uint32_t version() { return s_version; }

void sort_by_last_heard()
{
    std::sort(s_nodes, s_nodes + s_count, [](const Node &a, const Node &b) { return a.last_heard > b.last_heard; });
}

const char *display_name(uint32_t num, char *buf, size_t cap)
{
    Node *n = find(num);
    if (n && n->has_user && n->long_name[0]) strlcpy(buf, n->long_name, cap);
    else snprintf(buf, cap, "!%08x", (unsigned)num);
    return buf;
}

// ---- breadcrumbs -----------------------------------------------------------------
struct Trail { uint32_t num; uint8_t head, len; TrailPoint pts[TRAIL_LEN]; };
static const size_t TRAILS = 24;
static Trail s_trails[TRAILS];
static size_t s_trail_next = 0; // the slot a new node takes once all are used

static Trail *trail_slot(uint32_t num, bool create)
{
    for (size_t i = 0; i < TRAILS; i++) if (s_trails[i].len && s_trails[i].num == num) return &s_trails[i];
    if (!create) return nullptr;
    for (size_t i = 0; i < TRAILS; i++) if (!s_trails[i].len) { s_trails[i].num = num; s_trails[i].head = 0; return &s_trails[i]; }
    Trail *t = &s_trails[s_trail_next++ % TRAILS];
    t->num = num;
    t->head = t->len = 0;
    return t;
}

void trail_add(uint32_t num, int32_t lat_i, int32_t lon_i)
{
    Trail *t = trail_slot(num, true);
    if (t->len) {
        const TrailPoint &last = t->pts[(t->head + t->len - 1) % TRAIL_LEN];
        // About 25 m: 1e-7 degrees is 1.1 cm of latitude, longitude scaled by cos(lat)
        double dlat = (lat_i - last.lat_i) * 0.0111;
        double dlon = (lon_i - last.lon_i) * 0.0111 * cos(lat_i * 1e-7 * M_PI / 180.0);
        if (dlat * dlat + dlon * dlon < 25.0 * 25.0) return;
    }
    if (t->len < TRAIL_LEN) t->pts[(t->head + t->len++) % TRAIL_LEN] = {lat_i, lon_i};
    else { t->pts[t->head] = {lat_i, lon_i}; t->head = (t->head + 1) % TRAIL_LEN; }
}

size_t trail(uint32_t num, TrailPoint *out, size_t max)
{
    Trail *t = trail_slot(num, false);
    if (!t) return 0;
    size_t n = t->len < max ? t->len : max;
    for (size_t i = 0; i < n; i++) out[i] = t->pts[(t->head + i) % TRAIL_LEN];
    return n;
}

} // namespace nodedb
