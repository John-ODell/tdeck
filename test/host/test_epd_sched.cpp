// Host checks of the e-paper refresh rules in src/hal/epd_sched.h.
// Run: test/host/run.sh
#include <cstdio>
#include "hal/epd_sched.h"

using namespace epd;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static Rect R(int x0, int y0, int x1, int y1) { Rect r; r.x0 = x0; r.y0 = y0; r.x1 = x1; r.y1 = y1; return r; }

int main()
{
    static uint8_t a[FRAME_BYTES], b[FRAME_BYTES];
    memset(a, 0xFF, sizeof a);
    memset(b, 0xFF, sizeof b);

    // Identical frames: nothing to draw, so the refresh is skipped
    CHECK(diff_rect(a, b, full_rect()).empty());

    // One black pixel at (13, 100): the window widens to its byte, x 8..15
    b[100 * ROW_BYTES + 1] &= ~(0x80 >> 5);
    Rect d = diff_rect(a, b, full_rect());
    CHECK(d.x0 == 8 && d.x1 == 15 && d.y0 == 100 && d.y1 == 100);

    // A change outside the marked area is not looked for
    CHECK(diff_rect(a, b, R(0, 0, 239, 99)).empty());

    // Two changes: the window covers both
    b[300 * ROW_BYTES + 29] = 0x00;
    d = diff_rect(a, b, full_rect());
    CHECK(d.x0 == 8 && d.x1 == 239 && d.y0 == 100 && d.y1 == 300);

    // Marked areas off the screen are clipped
    CHECK(clip(R(-5, -5, 500, 500)).x1 == W - 1);
    CHECK(rect_union(Rect(), R(1, 2, 3, 4)).y1 == 4);

    // Coalescing: wait for a 30 ms pause, but never more than 250 ms
    Policy p;
    CHECK(!should_start(1010, 1000, 1000, p));
    CHECK(should_start(1030, 1000, 1000, p));
    CHECK(!should_start(1200, 1000, 1190, p));  // still typing
    CHECK(should_start(1250, 1000, 1240, p));   // cap reached
    CHECK(should_start(5, 0xFFFFFF00u, 0xFFFFFFF0u, p));  // millis() wrap

    // Waveform choice
    CHECK(choose_mode(FULL_NONE, 0, p) == MODE_PARTIAL);
    CHECK(choose_mode(FULL_NONE, 9, p) == MODE_PARTIAL);
    CHECK(choose_mode(FULL_NONE, 10, p) == MODE_FAST_FULL);
    CHECK(choose_mode(FULL_FAST, 0, p) == MODE_FAST_FULL);
    CHECK(choose_mode(FULL_SLOW, 12, p) == MODE_SLOW_FULL);
    Policy never; never.fast_full_every = 0;
    CHECK(choose_mode(FULL_NONE, 200, never) == MODE_PARTIAL);

    printf("%s (%d failures)\n", failures ? "FAIL" : "OK", failures);
    return failures ? 1 : 0;
}
