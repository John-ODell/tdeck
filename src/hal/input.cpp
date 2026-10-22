#include "input.h"
#include <Arduino.h>
#include <Wire.h>
#include "board.h"
#include "TouchDrvGT911.hpp"

static TouchDrvGT911 touch;
static bool s_kb = false, s_touch = false;
static lv_indev_t *s_keypad = nullptr, *s_pointer = nullptr;
static uint32_t s_last_activity = 0;
static input_key_hook_t s_hook = nullptr;

static volatile int16_t tb_up = 0, tb_down = 0, tb_left = 0, tb_right = 0;
static void IRAM_ATTR isr_up() { tb_up++; }
static void IRAM_ATTR isr_down() { tb_down++; }
static void IRAM_ATTR isr_left() { tb_left++; }
static void IRAM_ATTR isr_right() { tb_right++; }

// Small FIFO of keys waiting to be delivered as press/release pairs.
static uint32_t q[16];
static uint8_t q_head = 0, q_tail = 0;
static void push_key(uint32_t k)
{
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
        last = q[q_head];
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

static void touch_read(lv_indev_drv_t *, lv_indev_data_t *data)
{
    static int16_t x[1], y[1];
    data->state = LV_INDEV_STATE_REL;
    if (s_touch && touch.isPressed() && touch.getPoint(x, y, 1) > 0) {
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

    // Two edges per detent
    const int16_t step = 2;
    while (tb_up >= step) { noInterrupts(); tb_up -= step; interrupts(); push_key(LV_KEY_PREV); }
    while (tb_down >= step) { noInterrupts(); tb_down -= step; interrupts(); push_key(LV_KEY_NEXT); }
    while (tb_left >= step) { noInterrupts(); tb_left -= step; interrupts(); push_key(LV_KEY_LEFT); }
    while (tb_right >= step) { noInterrupts(); tb_right -= step; interrupts(); push_key(LV_KEY_RIGHT); }

    // Trackball click: short = ENTER, long (>600 ms) = ESC
    static uint32_t down_at = 0;
    static bool was_down = false, long_sent = false;
    bool down = digitalRead(BOARD_BOOT_PIN) == LOW;
    if (down && !was_down) { down_at = millis(); long_sent = false; }
    if (down && !long_sent && millis() - down_at > 600) { long_sent = true; push_key(LV_KEY_ESC); }
    if (!down && was_down && !long_sent && millis() - down_at > 30) push_key(LV_KEY_ENTER);
    was_down = down;
}

uint32_t input_last_activity_ms() { return s_last_activity; }
bool input_keyboard_found() { return s_kb; }
bool input_touch_found() { return s_touch; }
void input_set_key_hook(input_key_hook_t hook) { s_hook = hook; }

void input_set_group(lv_group_t *g)
{
    lv_group_set_default(g);
    if (s_keypad) lv_indev_set_group(s_keypad, g);
}
