// input.h for the T-Deck Pro: TCA8418 keyboard through the Pro keymap, CST66xx touch, and
// the BOOT button in the Plus trackball-click's lock role. Apps see the same keys and
// pointer as on the Plus; the trackball calls are no-ops here.
#include "input.h"
#include "hal/input_pro.h"
#include <Arduino.h>
#include "board.h"
#include "hal/tca8418.h"

static_assert(kb::K_UP == LV_KEY_UP && kb::K_DOWN == LV_KEY_DOWN && kb::K_LEFT == LV_KEY_LEFT &&
                  kb::K_RIGHT == LV_KEY_RIGHT && kb::K_ESC == LV_KEY_ESC && kb::K_DEL == LV_KEY_DEL &&
                  kb::K_BACKSPACE == LV_KEY_BACKSPACE && kb::K_ENTER == LV_KEY_ENTER &&
                  kb::K_NEXT == LV_KEY_NEXT && kb::K_PREV == LV_KEY_PREV,
              "keymap_pro.h key values must match LVGL");

static bool s_kb = false, s_touch = false;
static cst::Info s_touch_info;
static lv_indev_t *s_keypad = nullptr, *s_pointer = nullptr;
static uint32_t s_last_activity = 0;
static input_key_hook_t s_hook = nullptr;
static input_pro_key_log_t s_log = nullptr;
static bool s_locked = false;
static uint32_t s_blocked = 0;
static bool s_touch_on = true;
static input_lock_hook_t s_lock_hook = nullptr;
static kb::State s_mods;
static ProInputStats s_stats;

void input_set_locked(bool locked) { s_locked = locked; }
void input_set_touch(bool on) { s_touch_on = on; }
bool input_locked() { return s_locked; }
void input_set_lock_hook(input_lock_hook_t hook) { s_lock_hook = hook; }
void input_pro_set_key_log(input_pro_key_log_t log) { s_log = log; }

// Keys waiting to be delivered to LVGL as press/release pairs. 32 deep: a burst typed
// while LVGL is busy still fits.
static uint32_t q[32];
static uint8_t q_head = 0, q_tail = 0;
static void push_key(uint32_t k)
{
    if (s_locked) { s_blocked++; return; }  // a pocket press is not activity either
    s_last_activity = millis();
    if (s_hook && s_hook(k)) return;
    uint8_t next = (q_tail + 1) % 32;
    if (next == q_head) { s_stats.keys_dropped++; return; }
    q[q_tail] = k;
    q_tail = next;
    s_stats.keys_delivered++;
}

static uint32_t route_key(uint32_t k)
{
    if (k != LV_KEY_PREV && k != LV_KEY_NEXT) return k;
    lv_group_t *g = s_keypad ? s_keypad->group : nullptr;
    lv_obj_t *f = g ? lv_group_get_focused(g) : nullptr;
    if (f && lv_obj_check_type(f, &lv_dropdown_class) && lv_dropdown_is_open(f)) return k == LV_KEY_PREV ? LV_KEY_UP : LV_KEY_DOWN;
    return k;
}

static void keypad_read(lv_indev_drv_t *, lv_indev_data_t *data)
{
    static bool pressed = false;
    static uint32_t last = 0;
    if (pressed) {
        pressed = false;
        data->key = last;
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    if (q_head != q_tail) {
        last = route_key(q[q_head]);
        q_head = (q_head + 1) % 32;
        pressed = true;
        data->key = last;
        data->state = LV_INDEV_STATE_PR;
        data->continue_reading = true;
        return;
    }
    data->key = last;
    data->state = LV_INDEV_STATE_REL;
}

// ---------- keyboard ----------

static uint8_t s_rep_code = 0;   // key being held, for repeat
static uint32_t s_rep_key = 0, s_rep_at = 0, s_rep_done = 0;

static void on_key_event(uint8_t code, bool pressed)
{
    uint32_t now = millis();
    if (!pressed) {
        kb::release(s_mods, code, now);
        if (code == s_rep_code) s_rep_code = 0;
        return;
    }
    uint32_t k = kb::press(s_mods, code, now);
    if (s_locked) {
        if (code <= kb::CODE_MAX) s_blocked++;
        return;
    }
    s_last_activity = now;  // a modifier alone is activity too
    if (!k) return;
    if (s_log) s_log(code, k);
    push_key(k);
    if (kb::repeats(k)) { s_rep_code = code; s_rep_key = k; s_rep_at = now; s_rep_done = 0; }
    else s_rep_code = 0;
}

// ---------- touch ----------

static bool s_finger = false;
static int16_t s_fx = 0, s_fy = 0;
static uint32_t s_last_report = 0;
static int16_t s_tap_x = -1, s_tap_y = -1;
static uint32_t s_tap_until = 0;
void input_inject_tap(int x, int y) { s_tap_x = x; s_tap_y = y; s_tap_until = millis() + 80; }

static void touch_read(lv_indev_drv_t *, lv_indev_data_t *data)
{
    data->state = LV_INDEV_STATE_REL;
    if (s_tap_until) {
        if (millis() < s_tap_until) {
            data->state = LV_INDEV_STATE_PR;
            data->point.x = s_tap_x;
            data->point.y = s_tap_y;
            s_last_activity = millis();
        } else s_tap_until = 0;
        return;
    }
    static bool counted = false;
    if (!s_finger || !s_touch_on) { counted = false; return; }
    if (s_locked) {
        if (!counted) s_blocked++;
        counted = true;
        return;
    }
    data->state = LV_INDEV_STATE_PR;
    data->point.x = s_fx;
    data->point.y = s_fy;
    s_last_activity = millis();
}

static void poll_touch()
{
    if (!s_touch) return;
    if (cst::pending()) {
        bool down;
        int16_t x, y;
        if (cst::read(&down, &x, &y)) {
            s_last_report = millis();
            s_finger = down;
            if (down) {
                // Panel coordinates are already portrait 240x320 (factory firmware applies
                // no transform); scale only if the chip reports another resolution.
                if (s_touch_info.res_x && s_touch_info.res_x != SCREEN_W) x = (int32_t)x * SCREEN_W / s_touch_info.res_x;
                if (s_touch_info.res_y && s_touch_info.res_y != SCREEN_H) y = (int32_t)y * SCREEN_H / s_touch_info.res_y;
                s_fx = x < 0 ? 0 : x >= SCREEN_W ? SCREEN_W - 1 : x;
                s_fy = y < 0 ? 0 : y >= SCREEN_H ? SCREEN_H - 1 : y;
            }
        }
    }
    // The chip reports while a finger is down; silence means it lifted without a last report
    if (s_finger && millis() - s_last_report > 150) s_finger = false;
}

// ---------- BOOT button ----------
// Short press: Esc (back). Held a second: lock, or unlock when locked. Locked, only the
// hold does anything, so a pocket press changes nothing.
static void poll_boot()
{
    static uint32_t down_at = 0;
    static bool was_down = false, hold_sent = false;
    bool down = digitalRead(BOARD_BOOT_PIN) == LOW;
    uint32_t now = millis();
    if (down && !was_down) { down_at = now; hold_sent = false; }
    if (down && !hold_sent && now - down_at > 1000) {
        hold_sent = true;
        if (s_lock_hook) s_lock_hook(!s_locked);
    }
    if (!down && was_down && !hold_sent && now - down_at > 30) {
        if (s_locked) s_blocked++;
        else push_key(LV_KEY_ESC);
    }
    if (down != was_down && !s_locked) s_last_activity = now;
    was_down = down;
}

// ---------- input.h ----------

void input_init()
{
    pinMode(BOARD_BOOT_PIN, INPUT_PULLUP);
    s_kb = tca::init();
    s_touch = cst::init(&s_touch_info);

    static lv_indev_drv_t kp;
    lv_indev_drv_init(&kp);
    kp.type = LV_INDEV_TYPE_KEYPAD;
    kp.read_cb = keypad_read;
    s_keypad = lv_indev_drv_register(&kp);

    static lv_indev_drv_t tp;
    lv_indev_drv_init(&tp);
    tp.type = LV_INDEV_TYPE_POINTER;
    tp.read_cb = touch_read;
    s_pointer = lv_indev_drv_register(&tp);

    s_last_activity = millis();
}

void input_poll()
{
    if (s_kb) {
        // Drain on the interrupt, and every 50 ms anyway in case an edge was missed
        static uint32_t last_drain = 0, pending_since = 0;
        uint32_t now = millis();
        bool pend = tca::pending();
        if (pend && !pending_since) pending_since = now;
        if (pend || now - last_drain > 50) {
            tca::drain(on_key_event);
            if (pending_since && now - pending_since > s_stats.max_kb_wait_ms) s_stats.max_kb_wait_ms = now - pending_since;
            pending_since = 0;
            last_drain = now;
        }
        if (s_rep_code && !s_locked) {
            uint32_t n = kb::repeats_due(now, s_rep_at, s_rep_done);
            for (uint32_t i = 0; i < n; i++) push_key(s_rep_key);
            s_rep_done += n;
        }
    }
    poll_touch();
    poll_boot();
}

uint32_t input_last_activity_ms() { return s_last_activity; }
uint32_t input_blocked_count() { return s_blocked; }
bool input_keyboard_found() { return s_kb; }
bool input_touch_found() { return s_touch; }
void input_set_key_hook(input_key_hook_t hook) { s_hook = hook; }
void input_inject_key(uint32_t key) { push_key(key); }

void input_set_group(lv_group_t *g)
{
    lv_group_set_default(g);
    if (s_keypad) lv_indev_set_group(s_keypad, g);
}

// No trackball on the Pro
void input_set_trackball_mode(TrackballMode) {}
void input_trackball_take(int *dx, int *dy) { *dx = *dy = 0; }
bool input_trackball_pressed() { return false; }

ProInputStats input_pro_stats()
{
    ProInputStats s = s_stats;
    tca::Stats k = tca::stats();
    cst::Stats t = cst::stats();
    s.kb_events = k.events; s.kb_overflows = k.overflows; s.kb_edges = k.edges;
    s.touch_reports = t.reports; s.touch_checksum_errors = t.checksum_errors;
    s.touch_i2c_errors = t.i2c_errors; s.touch_edges = t.edges;
    return s;
}
const cst::Info &input_pro_touch_info() { return s_touch_info; }
const kb::State &input_pro_mods() { return s_mods; }
