#pragma once
#include <stdint.h>
#include <stddef.h>

// The I2S amplifier. One driver, shared by beeps and the music player.
void audio_tone(uint16_t freq_hz, uint16_t ms);    // queue a tone (freq 0 = a rest); plays from audio_tick()
void audio_tone_level(uint8_t level);              // 0 quiet, 1 normal, 2 loud, for every tone
void audio_tick();                                 // from the main loop: feed the queued tones to the DMA
bool audio_open(uint32_t rate_hz);                 // ready the amplifier at a sample rate
size_t audio_write(const int16_t *stereo, size_t frames); // frames written, 0 when the DMA is full (never blocks)
void audio_streaming();                            // the music player says it owns the amplifier right now
void audio_silence();                              // clear the DMA so nothing keeps looping
