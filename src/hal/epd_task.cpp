#include "hal/epd_task.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "hal/epd_uc8253.h"

namespace epd {

static uint8_t s_draw[FRAME_BYTES];   // what the UI wants
static uint8_t s_snap[FRAME_BYTES];   // copy being drawn
static uint8_t s_glass[FRAME_BYTES];  // what the panel shows
static SemaphoreHandle_t s_mutex;
static TaskHandle_t s_task;
static Policy s_policy;
static Stats s_stats;

// Guarded by s_mutex
static Rect s_dirty;
static uint32_t s_first_change, s_last_change;
static uint8_t s_full_req = FULL_NONE;
static bool s_change_while_idle;
static bool s_render_open;        // a render has flushed some chunks but not its last
static uint32_t s_render_open_since;

static uint8_t s_partials_since_full;
static bool s_first_frame = true;

uint8_t *frame_lock()
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    return s_draw;
}

void frame_unlock(Rect changed, bool frame_complete)
{
    changed = clip(changed);
    if (!frame_complete && !s_render_open) s_render_open_since = millis();
    s_render_open = !frame_complete;
    if (!changed.empty()) {
        uint32_t now = millis();
        if (s_dirty.empty() && s_full_req == FULL_NONE) {
            s_first_change = now;
            s_change_while_idle = !s_stats.refreshing;
        }
        s_last_change = now;
        s_dirty = rect_union(s_dirty, changed);
    }
    xSemaphoreGive(s_mutex);
    if (!changed.empty() && s_task) xTaskNotifyGive(s_task);
}

void request_full(FullRequest kind)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (kind > s_full_req) s_full_req = kind;
    if (s_dirty.empty()) s_first_change = s_last_change = millis();
    xSemaphoreGive(s_mutex);
    if (s_task) xTaskNotifyGive(s_task);
}

Stats stats()
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    Stats s = s_stats;
    xSemaphoreGive(s_mutex);
    return s;
}

void reset_stats()
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_stats = Stats();
    xSemaphoreGive(s_mutex);
}

bool idle()
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool i = s_dirty.empty() && s_full_req == FULL_NONE && !s_stats.refreshing;
    xSemaphoreGive(s_mutex);
    return i;
}

static void task(void *)
{
    uint32_t last_end = millis();
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));

        Rect rect;
        uint8_t full = FULL_NONE;
        bool go = false, idle_start = false;
        uint32_t first = 0;
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        // Read the clock under the lock: a time taken before waiting for it can be older
        // than the change just recorded, and the unsigned age would wrap to "long ago".
        uint32_t now = millis();
        // A full request waits out the same quiet window, so content LVGL is still
        // flushing lands in the full refresh instead of a partial right after it.
        // A render left open for over a second is treated as done (a lost last chunk).
        bool render_open = s_render_open && now - s_render_open_since < 1000;
        if ((s_full_req != FULL_NONE || !s_dirty.empty()) && !render_open &&
            should_start(now, s_first_change, s_last_change, s_policy)) {
            memcpy(s_snap, s_draw, FRAME_BYTES);
            rect = s_dirty;
            full = s_full_req;
            first = s_first_change;
            idle_start = s_change_while_idle;
            s_dirty = Rect();
            s_full_req = FULL_NONE;
            go = true;
            s_stats.refreshing = 1;
        }
        xSemaphoreGive(s_mutex);

        if (!go) {
            if (powered() && now - last_end >= s_policy.idle_power_off_ms) power_off();
            continue;
        }

        // Only a requested full redraws a frame that matches the glass
        Rect changed = diff_rect(s_snap, s_glass, rect);
        // The glass is unknown at boot: the first frame is a slow full, which clears it.
        if (s_first_frame) full = FULL_SLOW;
        Mode mode = choose_mode(full, s_partials_since_full, s_policy);
        Rect draw = mode == MODE_PARTIAL ? changed : (full != FULL_NONE || !changed.empty()) ? full_rect() : Rect();
        uint32_t latency = millis() - first;
        Timing t;
        bool drawn = !draw.empty() && refresh(s_snap, draw, mode, &t);
        if (drawn) memcpy(s_glass, s_snap, FRAME_BYTES);
        last_end = millis();

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_stats.refreshing = 0;
        if (!drawn) {
            s_stats.skipped++;
        } else {
            uint32_t ms = t.total_us / 1000;
            s_stats.last_total_ms = ms;
            s_stats.last_busy_ms = t.busy_us / 1000;
            s_stats.last_rect = draw;
            s_stats.last_end_ms = last_end;
            s_stats.last_start_latency_ms = latency;
            if (idle_start && latency > s_stats.max_start_latency_ms) s_stats.max_start_latency_ms = latency;
            if (mode == MODE_PARTIAL) {
                s_stats.partials++;
                if (ms > s_stats.max_partial_ms) s_stats.max_partial_ms = ms;
            } else if (mode == MODE_FAST_FULL) {
                s_stats.fast_fulls++;
                s_stats.last_fast_full_ms = ms;
            } else {
                s_stats.slow_fulls++;
                s_stats.last_slow_full_ms = ms;
            }
        }
        xSemaphoreGive(s_mutex);
        if (drawn) {
            s_partials_since_full = mode == MODE_PARTIAL ? s_partials_since_full + 1 : 0;
            s_first_frame = false;
        }
    }
}

bool task_start(const Policy &p)
{
    if (s_task) return true;
    s_policy = p;
    s_mutex = xSemaphoreCreateMutex();
    memset(s_draw, 0xFF, FRAME_BYTES);
    memset(s_glass, 0xFF, FRAME_BYTES);
    if (!hw_init()) return false;
    // The glass is unknown at boot: call it white. The first frame drawn is a slow full.
    set_old_plane(s_glass);
    // Core 0, below Wi-Fi and BLE; the Arduino loop (input, LVGL) stays on core 1.
    return xTaskCreatePinnedToCore(task, "epd", 4096, nullptr, 3, &s_task, 0) == pdPASS;
}

}  // namespace epd
