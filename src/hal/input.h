#pragma once
#include <stdint.h>
#include <lvgl.h>

// Keyboard + trackball share one LVGL keypad device; touch is a pointer device.
// Trackball: up/down move focus (PREV/NEXT), left/right go to the focused widget,
// click = ENTER, long click = ESC (back).
void input_init();
void input_poll();                 // call from loop(); drains keyboard and trackball
uint32_t input_last_activity_ms(); // for screen timeout
uint32_t input_blocked_count();    // input dropped while locked, for the "locked" hint
bool input_keyboard_found();
bool input_touch_found();
void input_set_group(lv_group_t *g);
// Raw key hook: return true to swallow the key before it reaches LVGL.
typedef bool (*input_key_hook_t)(uint32_t key);
void input_set_key_hook(input_key_hook_t hook);
void input_inject_key(uint32_t key);  // as if typed (debug console)
void input_inject_tap(int x, int y); // as if tapped, screen pixels (debug console)
// Locked (screen off): keys and trackball rolls are dropped and are not activity, so a
// pocket press changes nothing. Holding the trackball button about a second unlocks, and
// holding it two seconds while awake locks; both go through the lock hook.
void input_set_locked(bool locked);
void input_set_touch(bool on);     // off: the panel reports nothing (the Keyboard app, where a thumb lands on it)
bool input_locked();
typedef void (*input_lock_hook_t)(bool lock);
void input_set_lock_hook(input_lock_hook_t hook);

// Trackball modes. FOCUS: PREV/NEXT/LEFT/RIGHT steps (default). ARROWS: UP/DOWN/LEFT/RIGHT
// steps. RAW: no keys; edges accumulate for input_trackball_take() and a click is a held
// button (input_trackball_pressed) instead of ENTER, held as long as the finger stays, so
// only a 3 s hold is ESC there. In the other modes a 600 ms hold is ESC.
enum TrackballMode { TB_FOCUS, TB_ARROWS, TB_RAW };
void input_set_trackball_mode(TrackballMode m);
void input_trackball_take(int *dx, int *dy);
bool input_trackball_pressed();
