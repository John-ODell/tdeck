// ASCII (as the T-Deck keyboard sends it) to a USB/BLE HID keyboard usage + modifier.
#pragma once
#include <stdint.h>

#define HID_MOD_CTRL 0x01
#define HID_MOD_SHIFT 0x02

#define HID_ENTER 0x28
#define HID_ESC 0x29
#define HID_BACKSPACE 0x2A
#define HID_TAB 0x2B
#define HID_RIGHT 0x4F
#define HID_LEFT 0x50
#define HID_DOWN 0x51
#define HID_UP 0x52

// Returns false for characters with no key
static inline bool hid_from_ascii(uint8_t c, uint8_t *mod, uint8_t *usage)
{
    *mod = 0;
    if (c >= 'a' && c <= 'z') { *usage = 0x04 + c - 'a'; return true; }
    if (c >= 'A' && c <= 'Z') { *mod = HID_MOD_SHIFT; *usage = 0x04 + c - 'A'; return true; }
    if (c >= '1' && c <= '9') { *usage = 0x1E + c - '1'; return true; }
    switch (c) {
    case '0': *usage = 0x27; return true;
    case '\r': case '\n': *usage = HID_ENTER; return true;
    case 0x1B: *usage = HID_ESC; return true;
    case 0x08: *usage = HID_BACKSPACE; return true;
    case '\t': *usage = HID_TAB; return true;
    case ' ': *usage = 0x2C; return true;
    }
    // Unshifted and shifted symbols on the same key
    static const char plain[] = "-=[]\\;'`,./";
    static const char shifted[] = "_+{}|:\"~<>?";
    static const uint8_t usages[] = {0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38};
    for (int i = 0; plain[i]; i++) {
        if (c == (uint8_t)plain[i]) { *usage = usages[i]; return true; }
        if (c == (uint8_t)shifted[i]) { *mod = HID_MOD_SHIFT; *usage = usages[i]; return true; }
    }
    static const char digits_shifted[] = ")!@#$%^&*(";
    for (int i = 0; i < 10; i++)
        if (c == (uint8_t)digits_shifted[i]) { *mod = HID_MOD_SHIFT; *usage = i == 0 ? 0x27 : 0x1E + i - 1; return true; }
    // Other control codes (e.g. Alt+C sends 0x0C) become Ctrl+letter
    if (c >= 0x01 && c <= 0x1A) { *mod = HID_MOD_CTRL; *usage = 0x04 + c - 1; return true; }
    return false;
}
