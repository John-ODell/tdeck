// The T-Deck Pro display task. Callers draw into a shadow frame and return at once; a
// task on core 0 coalesces changes, skips frames that match the glass, picks the
// waveform and runs the refresh, so input and LVGL never wait for the panel.
#pragma once
#include <stdint.h>
#include "hal/epd_sched.h"

namespace epd {

struct Stats {
    uint32_t partials = 0, fast_fulls = 0, slow_fulls = 0;
    uint32_t skipped = 0;             // changes that matched the glass
    uint32_t last_total_ms = 0;       // last refresh, start to glass updated
    uint32_t max_partial_ms = 0;
    uint32_t last_busy_ms = 0;        // waveform time of the last refresh
    uint32_t last_fast_full_ms = 0, last_slow_full_ms = 0;
    Rect last_rect;                   // area drawn by the last refresh
    uint32_t last_end_ms = 0;          // millis() when the last refresh finished
    uint32_t last_start_latency_ms = 0;  // first pending change to refresh start
    uint32_t max_start_latency_ms = 0;   // only changes made while the panel was idle
    uint32_t refreshing = 0;          // 1 while a refresh runs
};

bool task_start(const Policy &p = Policy());

// Draw access to the shadow frame (bit set = white). Hold the lock only while drawing.
uint8_t *frame_lock();
// `changed` is the area drawn (empty: nothing). Pass frame_complete = false while a
// render is still mid-way (more LVGL chunks to come), so a refresh never shows half of it.
void frame_unlock(Rect changed, bool frame_complete = true);

void request_full(FullRequest kind);
Stats stats();
void reset_stats();
bool idle();  // nothing pending and no refresh running

}  // namespace epd
