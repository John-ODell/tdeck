#include "input.h"
#include <Arduino.h>
#include <Wire.h>
#include "board.h"
#include "core/settings.h"
#include "TouchDrvGT911.hpp"

static TouchDrvGT911 touch;
static bool s_kb = false, s_touch = false;
static lv_indev_t *s_keypad = nullptr, *s_pointer = nullptr;
static uint32_t s_last_activity = 0;
static input_key_hook_t s_hook = nullptr;
static bool s_locked = false;
static uint32_t s_blocked = 0;    // keys, rolls and taps dropped by the lock (#161)
static bool s_touch_on = true;
static bool s_raw_rolled = false;   // the ball moved during the current button hold (RAW mode)
static input_lock_hook_t s_lock_hook = nullptr;

void input_set_locked(bool locked) { s_locked = locked; }
void input_set_touch(bool on) { s_touch_on = on; }
bool input_locked() { return s_locked; }
void input_set_lock_hook(input_lock_hook_t hook) { s_lock_hook = hook; }

static volatile int16_t tb_up = 0, tb_down = 0, tb_left = 0, tb_right = 0;
static TrackballMode s_tb_mode = TB_FOCUS;
static int s_raw_dx = 0, s_raw_dy = 0;
static bool s_tb_pressed = false;
static void IRAM_ATTR isr_up() { tb_up++; }
static void IRAM_ATTR isr_down() { tb_down++; }
static void IRAM_ATTR isr_left() { tb_left++; }
static void IRAM_ATTR isr_right() { tb_right++; }

// Small FIFO of keys waiting to be delivered as press/release pairs.
static uint32_t q[16];
static uint8_t q_head = 0, q_tail = 0;
static void push_key(uint32_t k)
{
    if (s_locked) { s_blocked++; return; } // a pocket press is not activity either
    s_last_activity = millis();
    if (s_hook && s_hook(k)) return;
    uint8_t next = (q_tail + 1) % 16;
    if (next == q_head) return;
    q[q_tail] = k;
    q_tail = next;
}

static uint32_t map_kb(uint8_t c)
{
    switch (c) {
    case 0x0D: case 0x0A: return LV_KEY_ENTER;
    case 0x08: return LV_KEY_BACKSPACE;
    case 0x1B: return LV_KEY_ESC;
    default: return c;
    }
}

// An open dropdown list takes up/down itself; PREV/NEXT would jump focus away and close it
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
        q_head = (q_head + 1) % 16;
        pressed = true;
        data->key = last;
        data->state = LV_INDEV_STATE_PR;
        data->continue_reading = true;
        return;
    }
    data->key = last;
    data->state = LV_INDEV_STATE_REL;
}

static int16_t s_tap_x = -1, s_tap_y = -1;
static uint32_t s_tap_until = 0;
void input_inject_tap(int x, int y) { s_tap_x = x; s_tap_y = y; s_tap_until = millis() + 80; }

static void touch_read(lv_indev_drv_t *, lv_indev_data_t *data)
{
    static int16_t x[1], y[1];
    data->state = LV_INDEV_STATE_REL;
    if (s_tap_until) { // a console tap: pressed at one point for 80 ms, then released
        if (millis() < s_tap_until) {
            data->state = LV_INDEV_STATE_PR;
            data->point.x = s_tap_x;
            data->point.y = s_tap_y;
            s_last_activity = millis();
        } else s_tap_until = 0;
        return;
    }
    if (s_touch && s_touch_on && touch.isPressed() && touch.getPoint(x, y, 1) > 0) {
        static bool was_down = false;
        if (s_locked) {
            // Not activity either, or a locked screen lit for a message stays lit under a
            // tapping finger and looks alive while it drops everything (#161)
            if (!was_down) s_blocked++;
            was_down = true;
            return;
        }
        was_down = false;
        data->state = LV_INDEV_STATE_PR;
        data->point.x = x[0];
        data->point.y = y[0];
        s_last_activity = millis();
    }
}

void input_init()
{
    pinMode(BOARD_TBOX_UP, INPUT_PULLUP);
    pinMode(BOARD_TBOX_DOWN, INPUT_PULLUP);
    pinMode(BOARD_TBOX_LEFT, INPUT_PULLUP);
    pinMode(BOARD_TBOX_RIGHT, INPUT_PULLUP);
    pinMode(BOARD_BOOT_PIN, INPUT_PULLUP);
    attachInterrupt(BOARD_TBOX_UP, isr_up, CHANGE);
    attachInterrupt(BOARD_TBOX_DOWN, isr_down, CHANGE);
    attachInterrupt(BOARD_TBOX_LEFT, isr_left, CHANGE);
    attachInterrupt(BOARD_TBOX_RIGHT, isr_right, CHANGE);

    for (int i = 0; i < 3 && !s_kb; i++) {
        Wire.requestFrom(KEYBOARD_I2C_ADDR, 1);
        s_kb = Wire.read() != -1;
    }

    // GT911 answers on 0x5D or 0x14 depending on INT level at reset
    touch.setPins(-1, BOARD_TOUCH_INT);
    s_touch = touch.begin(Wire, GT911_SLAVE_ADDRESS_L, BOARD_I2C_SDA, BOARD_I2C_SCL) ||
              touch.begin(Wire, GT911_SLAVE_ADDRESS_H, BOARD_I2C_SDA, BOARD_I2C_SCL);
    if (s_touch) {
        touch.setMaxCoordinates(SCREEN_W, SCREEN_H);
        touch.setSwapXY(true);
        touch.setMirrorXY(false, true);
    }

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
        static uint32_t last_kb = 0;
        if (millis() - last_kb > 20) {
            last_kb = millis();
            if (Wire.requestFrom(KEYBOARD_I2C_ADDR, 1) == 1) {
                int c = Wire.read();
                if (c > 0) push_key(map_kb((uint8_t)c));
            }
        }
    }

    if (s_tb_mode == TB_RAW) {
        noInterrupts();
        int u = tb_up, d = tb_down, l = tb_left, r = tb_right;
        tb_up = tb_down = tb_left = tb_right = 0;
        interrupts();
        if ((u | d | l | r) && s_locked) s_blocked++;
        if ((u | d | l | r) && !s_locked) {
            s_last_activity = millis();
            s_raw_dx += r - l;
            s_raw_dy += d - u;
            s_raw_rolled = true;
        }
    } else {
    // The hall sensors toggle many times per roll, so one step needs several edges.
    // Partial counts are dropped after a pause, and a step on one axis clears the
    // other so a slightly diagonal roll doesn't also move sideways.
    // A minimum gap between steps keeps a fast flick from racing through long lists.
    static const int16_t EDGES[] = {10, 7, 5, 3, 2};          // by settings.trackball 1..5
    static const uint16_t GAP_MS[] = {220, 160, 120, 80, 50};
    const int sp = (settings.trackball >= 1 && settings.trackball <= 5 ? settings.trackball : 3) - 1;
    const int16_t step = EDGES[sp];
    static uint32_t last_fire_ms = 0;
    const bool rate_ok = millis() - last_fire_ms >= GAP_MS[sp];
    static uint32_t last_edge_ms = 0;
    static int16_t seen = 0;
    int16_t total = tb_up + tb_down + tb_left + tb_right;
    if (total != seen) { seen = total; last_edge_ms = millis(); }
    if (total && millis() - last_edge_ms > 250) {
        noInterrupts(); tb_up = tb_down = tb_left = tb_right = 0; interrupts();
        seen = 0;
    }
    auto fire = [&](volatile int16_t &axis, volatile int16_t &a, volatile int16_t &b, volatile int16_t &c, uint32_t key) {
        if (axis < step) return;
        if (!rate_ok) {
            // Hold at most one pending step so nothing keeps scrolling after the roll stops
            noInterrupts(); if (axis > step) axis = step; interrupts();
            seen = tb_up + tb_down + tb_left + tb_right;
            return;
        }
        noInterrupts(); axis -= step; a = b = c = 0; interrupts();
        seen = axis;
        last_fire_ms = millis();
        push_key(key);
    };
    const bool arrows = s_tb_mode == TB_ARROWS;
    fire(tb_up, tb_down, tb_left, tb_right, arrows ? LV_KEY_UP : LV_KEY_PREV);
    fire(tb_down, tb_up, tb_left, tb_right, arrows ? LV_KEY_DOWN : LV_KEY_NEXT);
    fire(tb_left, tb_up, tb_down, tb_right, LV_KEY_LEFT);
    fire(tb_right, tb_up, tb_down, tb_left, LV_KEY_RIGHT);
    }

    // Trackball click: short = ENTER, long (>600 ms) = ESC, held 2 s = lock.
    // Locked, only a hold of about a second does anything: it unlocks.
    static uint32_t down_at = 0;
    static bool was_down = false, long_sent = false, lock_sent = false;
    bool down = digitalRead(BOARD_BOOT_PIN) == LOW;
    if (down && !was_down) { down_at = millis(); long_sent = lock_sent = false; s_raw_rolled = false; }
    if (s_locked) {
        if (down && !lock_sent && millis() - down_at > 1000) {
            lock_sent = long_sent = true;   // the release after an unlock is not a click
            if (s_lock_hook) s_lock_hook(false);
        }
    } else if (s_tb_mode == TB_RAW) {
        // Mouse mode: the hold scrolls or drags (#48, #123). Only a still 3 s hold does
        // anything else: it leaves typing (#119).
        if (down && !long_sent && !s_raw_rolled && millis() - down_at > 3000) { long_sent = true; push_key(LV_KEY_ESC); }
        if (down != was_down) s_last_activity = millis();
    } else {
        if (down && !long_sent && millis() - down_at > 600) { long_sent = true; push_key(LV_KEY_ESC); }
        if (down && !lock_sent && millis() - down_at > 2000) { lock_sent = true; if (s_lock_hook) s_lock_hook(true); }
        if (!down && was_down && !long_sent && millis() - down_at > 30 && s_tb_mode != TB_RAW) push_key(LV_KEY_ENTER);
        if (down != was_down) s_last_activity = millis();
    }
    was_down = down;
    s_tb_pressed = down && !long_sent && !s_locked;
}

uint32_t input_last_activity_ms() { return s_last_activity; }
uint32_t input_blocked_count() { return s_blocked; }
bool input_keyboard_found() { return s_kb; }
bool input_touch_found() { return s_touch; }
void input_set_key_hook(input_key_hook_t hook) { s_hook = hook; }
void input_inject_key(uint32_t key) { push_key(key); }

void input_set_trackball_mode(TrackballMode m)
{
    noInterrupts();
    tb_up = tb_down = tb_left = tb_right = 0;
    interrupts();
    s_raw_dx = s_raw_dy = 0;
    s_tb_mode = m;
}

void input_trackball_take(int *dx, int *dy)
{
    *dx = s_raw_dx;
    *dy = s_raw_dy;
    s_raw_dx = s_raw_dy = 0;
}

bool input_trackball_pressed() { return s_tb_pressed; }

void input_set_group(lv_group_t *g)
{
    lv_group_set_default(g);
    if (s_keypad) lv_indev_set_group(s_keypad, g);
}
