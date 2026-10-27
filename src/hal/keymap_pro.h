// T-Deck Pro keyboard map and modifier rules. Pure C++ so the host tests can check every
// key (test/host/test_keymap_pro.cpp); input_pro.cpp feeds it TCA8418 events.
//
// Key codes are the TCA8418's (1..35, row * 10 + column + 1). Names and layers follow the
// key legends as Meshtastic's TDeckProKeyboard.cpp maps them; LilyGo's factory map has
// letters only. Layers:
//   base  letters, Backspace, Enter, '$', Space; Mic is Esc (back), as nothing else is
//   Shift capitals
//   Sym   the symbol printed on each key; Sym+Mic is '0'
//   Alt   navigation, since there are no arrow keys: E up, X down, S left, F right,
//         Q Esc, T next (Tab), Backspace forward delete
// A modifier works held (chord), tapped once (applies to the next key) or tapped twice
// within 400 ms (locked until tapped again).
#pragma once
#include <stdint.h>

namespace kb {

// LVGL 8 key values (lv_group.h), repeated so this header needs no LVGL.
enum : uint32_t {
    K_NONE = 0, K_HOME = 2, K_END = 3, K_BACKSPACE = 8, K_NEXT = 9, K_ENTER = 10, K_PREV = 11,
    K_UP = 17, K_DOWN = 18, K_RIGHT = 19, K_LEFT = 20, K_ESC = 27, K_DEL = 127,
};

enum Code : uint8_t {
    CODE_BACKSPACE = 11, CODE_ENTER = 21, CODE_DOLLAR = 22, CODE_ALT = 30, CODE_RSHIFT = 31,
    CODE_SYM = 32, CODE_SPACE = 33, CODE_MIC = 34, CODE_LSHIFT = 35, CODE_MAX = 35,
};

// Index = code - 1. 0 = no output on this layer.
static const uint32_t BASE[35] = {
    'p', 'o', 'i', 'u', 'y', 't', 'r', 'e', 'w', 'q',
    K_BACKSPACE, 'l', 'k', 'j', 'h', 'g', 'f', 'd', 's', 'a',
    K_ENTER, '$', 'm', 'n', 'b', 'v', 'c', 'x', 'z', 0,
    0, 0, ' ', K_ESC, 0,
};
static const uint32_t SYM[35] = {
    '@', '+', '-', '_', ')', '(', '3', '2', '1', '#',
    K_BACKSPACE, '"', '\'', ';', ':', '/', '6', '5', '4', '*',
    K_ENTER, '$', '.', ',', '!', '?', '9', '8', '7', 0,
    0, 0, ' ', '0', 0,
};
static const uint32_t ALT[35] = {
    0, 0, 0, 0, 0, K_NEXT, 0, K_UP, 0, K_ESC,
    K_DEL, 0, 0, 0, 0, 0, K_RIGHT, 0, K_LEFT, 0,
    K_ENTER, 0, 0, 0, 0, 0, 0, K_DOWN, 0, 0,
    0, 0, ' ', K_ESC, 0,
};

enum Mod : uint8_t { MOD_SHIFT = 0, MOD_SYM = 1, MOD_ALT = 2, MOD_COUNT = 3 };

inline int mod_of(uint8_t code)
{
    switch (code) {
    case CODE_LSHIFT: case CODE_RSHIFT: return MOD_SHIFT;
    case CODE_SYM: return MOD_SYM;
    case CODE_ALT: return MOD_ALT;
    default: return -1;
    }
}

static const uint32_t DOUBLE_TAP_MS = 400;
static const uint32_t REPEAT_DELAY_MS = 450;
static const uint32_t REPEAT_EVERY_MS = 60;

struct ModState {
    uint8_t held = 0;        // shift keys counted separately (both may be down)
    bool used = false;       // another key went down while this one was held
    bool latched = false;    // one-shot: applies to the next key
    bool locked = false;
    uint32_t last_tap = 0;
};

struct State {
    ModState mod[MOD_COUNT];
};

inline bool active(const State &s, int m) { return s.mod[m].held || s.mod[m].latched || s.mod[m].locked; }

// A key went down. Returns the key to deliver (K_NONE for modifiers and unmapped keys).
inline uint32_t press(State &s, uint8_t code, uint32_t now)
{
    if (code < 1 || code > CODE_MAX) return K_NONE;
    int m = mod_of(code);
    if (m >= 0) {
        if (!s.mod[m].held) s.mod[m].used = false;
        s.mod[m].held++;
        (void)now;
        return K_NONE;
    }
    uint32_t out;
    if (active(s, MOD_ALT)) out = ALT[code - 1];
    else if (active(s, MOD_SYM)) out = SYM[code - 1];
    else out = BASE[code - 1];
    if (active(s, MOD_SHIFT) && out >= 'a' && out <= 'z') out = out - 'a' + 'A';
    for (int i = 0; i < MOD_COUNT; i++) {
        if (s.mod[i].held) s.mod[i].used = true;
        s.mod[i].latched = false;  // one-shot spent
    }
    return out;
}

// A key came up. Modifiers latch or lock here.
inline void release(State &s, uint8_t code, uint32_t now)
{
    int m = mod_of(code);
    if (m < 0) return;
    ModState &ms = s.mod[m];
    if (ms.held) ms.held--;
    if (ms.held || ms.used) return;  // a chord: no latch
    if (ms.locked) {
        ms.locked = false;
    } else if (ms.latched && now - ms.last_tap < DOUBLE_TAP_MS) {
        ms.latched = false;
        ms.locked = true;
    } else {
        ms.latched = !ms.latched;
    }
    ms.last_tap = now;
}

// Keys that repeat while held: characters, Backspace, Delete and the arrows.
inline bool repeats(uint32_t key)
{
    if (key >= ' ' && key < K_DEL) return true;
    return key == K_BACKSPACE || key == K_DEL || key == K_UP || key == K_DOWN || key == K_LEFT || key == K_RIGHT;
}

// How many repeats are due for a key held since `pressed_at`, given `done` already sent.
inline uint32_t repeats_due(uint32_t now, uint32_t pressed_at, uint32_t done)
{
    uint32_t held = now - pressed_at;
    if (held < REPEAT_DELAY_MS) return 0;
    uint32_t total = 1 + (held - REPEAT_DELAY_MS) / REPEAT_EVERY_MS;
    return total > done ? total - done : 0;
}

// Short text for a status bar: "Aa" shift latched, "AA" locked, "Sym", "SYM", "Alt", "ALT".
inline const char *mod_label(const State &s, int m)
{
    static const char *const one[MOD_COUNT] = {"Aa", "Sym", "Alt"};
    static const char *const lock[MOD_COUNT] = {"AA", "SYM", "ALT"};
    if (s.mod[m].locked) return lock[m];
    if (s.mod[m].latched || s.mod[m].held) return one[m];
    return "";
}

}  // namespace kb
