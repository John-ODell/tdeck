// Host checks of the T-Deck Pro keymap and modifier rules in src/hal/keymap_pro.h.
// Run: test/host/run.sh
#include <cstdio>
#include <cstring>
#include "hal/keymap_pro.h"

using namespace kb;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static uint32_t tap(State &s, uint8_t code, uint32_t &t)
{
    uint32_t k = press(s, code, t);
    t += 50;
    release(s, code, t);
    t += 50;
    return k;
}

int main()
{
    uint32_t t = 1000;

    // Every key on the base layer, codes 1..35
    const char *letters = "poiuytrewq" "_lkjhgfdsa" "_$mnbvcxz_" "_____";
    State s;
    for (int code = 1; code <= 35; code++) {
        char want = letters[code - 1];
        uint32_t got = tap(s, code, t);
        if (want != '_') CHECK(got == (uint32_t)want);
    }
    CHECK(tap(s, CODE_BACKSPACE, t) == K_BACKSPACE);
    CHECK(tap(s, CODE_ENTER, t) == K_ENTER);
    CHECK(tap(s, CODE_SPACE, t) == ' ');
    CHECK(tap(s, CODE_MIC, t) == K_ESC);
    CHECK(BASE[CODE_ALT - 1] == 0 && BASE[CODE_SYM - 1] == 0 && BASE[CODE_LSHIFT - 1] == 0);

    // Every letter has a capital with Shift held, and every symbol key has its legend with Sym held
    const char *sym = "@+-_)(321#" "\x01\"';:/654*" "\x01$.,!?987\x01" "\x01\x01 0\x01";
    for (int code = 1; code <= 35; code++) {
        if (mod_of(code) >= 0) continue;
        State a;
        press(a, CODE_LSHIFT, t);
        uint32_t k = press(a, code, t);
        uint32_t base = BASE[code - 1];
        if (base >= 'a' && base <= 'z') CHECK(k == base - 'a' + 'A');
        State b;
        press(b, CODE_SYM, t);
        k = press(b, code, t);
        if (sym[code - 1] != '\x01') CHECK(k == (uint32_t)(unsigned char)sym[code - 1]);
    }

    // Alt is navigation: E/X/S/F arrows, Q Esc, T Tab, Backspace delete
    State n;
    press(n, CODE_ALT, t);
    CHECK(press(n, 8, t) == K_UP);      // e
    CHECK(press(n, 28, t) == K_DOWN);   // x
    CHECK(press(n, 19, t) == K_LEFT);   // s
    CHECK(press(n, 17, t) == K_RIGHT);  // f
    CHECK(press(n, 10, t) == K_ESC);    // q
    CHECK(press(n, 6, t) == K_NEXT);    // t
    CHECK(press(n, CODE_BACKSPACE, t) == K_DEL);
    CHECK(press(n, 1, t) == K_NONE);    // p: nothing on the Alt layer
    release(n, CODE_ALT, t);
    CHECK(!active(n, MOD_ALT));         // used in a chord: no latch

    // Shift tapped once: the next letter only
    State o;
    t += 1000;
    tap(o, CODE_RSHIFT, t);
    CHECK(strcmp(mod_label(o, MOD_SHIFT), "Aa") == 0);
    CHECK(tap(o, 20, t) == 'A');
    CHECK(tap(o, 20, t) == 'a');

    // Shift tapped twice: caps lock until tapped again
    State c;
    t += 1000;
    tap(c, CODE_LSHIFT, t);
    tap(c, CODE_LSHIFT, t);
    CHECK(strcmp(mod_label(c, MOD_SHIFT), "AA") == 0);
    CHECK(tap(c, 20, t) == 'A');
    CHECK(tap(c, 19, t) == 'S');
    CHECK(tap(c, CODE_SPACE, t) == ' ');
    tap(c, CODE_LSHIFT, t);
    CHECK(tap(c, 20, t) == 'a');

    // Two taps far apart: latch on, then off, not a lock
    State f;
    t += 1000;
    tap(f, CODE_SYM, t);
    t += 1000;
    tap(f, CODE_SYM, t);
    CHECK(!active(f, MOD_SYM));
    CHECK(tap(f, 7, t) == 'r');

    // Sym one-shot, then a digit row
    State d;
    tap(d, CODE_SYM, t);
    CHECK(tap(d, 9, t) == '1');
    CHECK(tap(d, 9, t) == 'w');

    // Both shifts held then one released: still shifted
    State h;
    press(h, CODE_LSHIFT, t);
    press(h, CODE_RSHIFT, t);
    release(h, CODE_RSHIFT, t);
    CHECK(press(h, 23, t) == 'M');

    // Repeat: nothing before 450 ms, then one every 60 ms
    CHECK(repeats_due(1000 + 449, 1000, 0) == 0);
    CHECK(repeats_due(1000 + 450, 1000, 0) == 1);
    CHECK(repeats_due(1000 + 570, 1000, 1) == 2);
    CHECK(repeats_due(1000 + 570, 1000, 3) == 0);
    CHECK(repeats('a') && repeats(K_BACKSPACE) && repeats(K_DOWN) && repeats(' '));
    CHECK(!repeats(K_ENTER) && !repeats(K_ESC) && !repeats(K_NEXT));

    // Codes outside the matrix are ignored
    State z;
    CHECK(press(z, 0, t) == K_NONE && press(z, 36, t) == K_NONE);

    printf("%s (%d failures)\n", failures ? "FAIL" : "OK", failures);
    return failures ? 1 : 0;
}
