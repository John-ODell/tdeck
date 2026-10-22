#pragma once
#include <stdint.h>
#include <lvgl.h>

// Keyboard + trackball share one LVGL keypad device; touch is a pointer device.
// Trackball: up/down move focus (PREV/NEXT), left/right go to the focused widget,
// click = ENTER, long click = ESC (back).
void input_init();
void input_poll();                 // call from loop(); drains keyboard and trackball
uint32_t input_last_activity_ms(); // for screen timeout
bool input_keyboard_found();
bool input_touch_found();
void input_set_group(lv_group_t *g);
// Raw key hook: return true to swallow the key before it reaches LVGL.
typedef bool (*input_key_hook_t)(uint32_t key);
void input_set_key_hook(input_key_hook_t hook);
// While the screen is off, input only counts as activity (the waking key is dropped)
void input_set_screen_off(bool off);
