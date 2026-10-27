// Refresh scheduling rules for the T-Deck Pro e-paper. Pure C++ so the host tests can
// check them (test/host/test_epd_sched.cpp); the display task in epd_task.cpp uses them.
#pragma once
#include <stdint.h>
#include <string.h>

namespace epd {

static const int W = 240, H = 320;
static const int ROW_BYTES = W / 8;            // 30; bit set = white
static const int FRAME_BYTES = ROW_BYTES * H;  // 9600

// Inclusive pixel rectangle; empty when x1 < x0.
struct Rect {
    int16_t x0 = 0, y0 = 0, x1 = -1, y1 = -1;
    bool empty() const { return x1 < x0 || y1 < y0; }
};

inline Rect full_rect() { Rect r; r.x0 = 0; r.y0 = 0; r.x1 = W - 1; r.y1 = H - 1; return r; }

inline Rect rect_union(const Rect &a, const Rect &b)
{
    if (a.empty()) return b;
    if (b.empty()) return a;
    Rect r;
    r.x0 = a.x0 < b.x0 ? a.x0 : b.x0;
    r.y0 = a.y0 < b.y0 ? a.y0 : b.y0;
    r.x1 = a.x1 > b.x1 ? a.x1 : b.x1;
    r.y1 = a.y1 > b.y1 ? a.y1 : b.y1;
    return r;
}

inline Rect clip(Rect r)
{
    if (r.x0 < 0) r.x0 = 0;
    if (r.y0 < 0) r.y0 = 0;
    if (r.x1 > W - 1) r.x1 = W - 1;
    if (r.y1 > H - 1) r.y1 = H - 1;
    return r;
}

// The part of `within` where the two frames differ, widened to whole bytes (8 px),
// which is the controller's window granularity. Empty when nothing changed.
inline Rect diff_rect(const uint8_t *a, const uint8_t *b, Rect within)
{
    Rect out;
    within = clip(within);
    if (within.empty()) return out;
    int bx0 = within.x0 / 8, bx1 = within.x1 / 8;
    for (int y = within.y0; y <= within.y1; y++) {
        const uint8_t *ra = a + y * ROW_BYTES, *rb = b + y * ROW_BYTES;
        if (memcmp(ra + bx0, rb + bx0, bx1 - bx0 + 1) == 0) continue;
        int l = bx0, r = bx1;
        while (ra[l] == rb[l]) l++;
        while (ra[r] == rb[r]) r--;
        Rect row;
        row.x0 = l * 8; row.x1 = r * 8 + 7; row.y0 = row.y1 = y;
        out = rect_union(out, row);
    }
    return out;
}

enum Mode : uint8_t {
    MODE_PARTIAL,    // factory TR7 waveform, about 645 ms
    MODE_FAST_FULL,  // factory TR5 waveform, about 1 s, whole screen
    MODE_SLOW_FULL,  // real-temperature waveform, about 3 s, clears ghosting
};

enum FullRequest : uint8_t { FULL_NONE = 0, FULL_FAST = 1, FULL_SLOW = 2 };

struct Policy {
    uint16_t quiet_ms = 30;         // start once changes pause this long...
    uint16_t cap_ms = 250;          // ...or this long after the first change, whichever is first
    uint8_t fast_full_every = 10;   // partials between fast fulls (the panel's protection)
    uint16_t idle_power_off_ms = 1500;
};

// Whether a pending change should be drawn now. Times are millis(); wrap-safe.
inline bool should_start(uint32_t now, uint32_t first_change, uint32_t last_change, const Policy &p)
{
    return (uint32_t)(now - last_change) >= p.quiet_ms || (uint32_t)(now - first_change) >= p.cap_ms;
}

// The waveform for the next refresh. A requested full wins; otherwise a fast full
// replaces every Nth partial.
inline Mode choose_mode(uint8_t full_request, uint8_t partials_since_full, const Policy &p)
{
    if (full_request == FULL_SLOW) return MODE_SLOW_FULL;
    if (full_request == FULL_FAST) return MODE_FAST_FULL;
    if (p.fast_full_every && partials_since_full >= p.fast_full_every) return MODE_FAST_FULL;
    return MODE_PARTIAL;
}

}  // namespace epd
