// Host stand-in for ESP-IDF's RNG (test use only)
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
static inline uint32_t esp_random() { return arc4random(); }
static inline void esp_fill_random(void *buf, size_t n) { arc4random_buf(buf, n); }
