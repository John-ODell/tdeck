#pragma once
#include <stdint.h>
#include <stddef.h>

// The I2S amplifier. One driver, shared by beeps and the music player.
void audio_beep(uint16_t freq_hz, uint16_t ms);    // blocks for its length, so keep them short
bool audio_open(uint32_t rate_hz);                 // ready the amplifier at a sample rate
size_t audio_write(const int16_t *stereo, size_t frames); // frames written, 0 when the DMA is full (never blocks)
void audio_silence();                              // clear the DMA so nothing keeps looping
