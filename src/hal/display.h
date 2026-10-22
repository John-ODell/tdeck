#pragma once
#include <stdint.h>

void display_init();
void display_set_brightness(uint8_t level);  // 0..16, 0 = off
uint8_t display_get_brightness();
