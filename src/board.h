// Board selection. Every source file includes this; the pin map comes from the board
// picked at compile time, and the board API (src/board/board_api.h) is shared.
#pragma once
#if defined(BOARD_TDECK_PRO)
#include "board/pins_pro.h"
#else
#include "board/pins_plus.h"
#endif
#include "board/board_api.h"
