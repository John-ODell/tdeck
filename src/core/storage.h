#pragma once
#include <stdint.h>

// microSD on the shared SPI bus, mounted at /sd (LVGL drive "S:")
bool sd_mount();
bool sd_ok();
uint64_t sd_total_bytes();
uint64_t sd_used_bytes();
