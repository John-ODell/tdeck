// Host checks of the board revision and capability rules in src/board/caps.h.
// Run: test/host/run.sh
#include <cstdio>
#include <initializer_list>
#include "board/caps.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

int main()
{
    // Revision from the two probes
    CHECK(board_rev_from_probe(false, true) == BOARD_REV_PRO_V1_1);
    CHECK(board_rev_from_probe(true, false) == BOARD_REV_PRO_V1_0);
    CHECK(board_rev_from_probe(false, false) == BOARD_REV_PRO_UNKNOWN);
    CHECK(board_rev_from_probe(true, true) == BOARD_REV_PRO_UNKNOWN);

    uint32_t plus = board_caps_for(BOARD_MODEL_TDECK_PLUS, BOARD_REV_NA);
    uint32_t v11 = board_caps_for(BOARD_MODEL_TDECK_PRO, BOARD_REV_PRO_V1_1);
    uint32_t v10 = board_caps_for(BOARD_MODEL_TDECK_PRO, BOARD_REV_PRO_V1_0);
    uint32_t unk = board_caps_for(BOARD_MODEL_TDECK_PRO, BOARD_REV_PRO_UNKNOWN);

    // The Plus has a colour LCD, a trackball and a speaker; no e-paper, gauge or modem
    CHECK((plus & (CAP_COLOR_LCD | CAP_BACKLIGHT | CAP_TRACKBALL | CAP_I2S_SPEAKER)) ==
          (CAP_COLOR_LCD | CAP_BACKLIGHT | CAP_TRACKBALL | CAP_I2S_SPEAKER));
    CHECK(!(plus & (CAP_EPAPER | CAP_FUEL_GAUGE | CAP_MODEM | CAP_HAPTICS)));

    // The Pro never has a backlight, trackball or I2S speaker (the 4G SKU)
    for (uint32_t c : {v10, v11, unk}) {
        CHECK(c & CAP_EPAPER);
        CHECK(c & CAP_FUEL_GAUGE);
        CHECK(c & CAP_LORA);
        CHECK(!(c & (CAP_COLOR_LCD | CAP_BACKLIGHT | CAP_TRACKBALL | CAP_I2S_SPEAKER)));
    }
    CHECK(v11 & CAP_HAPTICS);
    CHECK(!(v11 & CAP_LIGHT_SENSOR));
    CHECK(v10 & CAP_LIGHT_SENSOR);
    CHECK(!(unk & CAP_HAPTICS));  // unknown revision: do not drive GPIO2

    printf("%s (%d failures)\n", failures ? "FAIL" : "OK", failures);
    return failures ? 1 : 0;
}
