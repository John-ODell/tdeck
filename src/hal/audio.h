#pragma once
#include <stdint.h>

// The I2S amplifier. A beep blocks for its length, so keep them short.
void audio_beep(uint16_t freq_hz, uint16_t ms);
