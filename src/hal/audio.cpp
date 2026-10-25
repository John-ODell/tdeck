#include "audio.h"
#include <Arduino.h>
#include <math.h>
#include <driver/i2s.h>
#include "board.h"

static bool s_ready = false;
static uint32_t s_rate = 0;

bool audio_open(uint32_t rate_hz)
{
    if (!s_ready) {
        i2s_config_t cfg = {};
        cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
        cfg.sample_rate = rate_hz;
        cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
        cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
        cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
        cfg.intr_alloc_flags = 0;
        // Enough buffered sound to ride out a full screen redraw: 8 x 1024 frames is 190 ms at 44.1 kHz
        cfg.dma_buf_count = 8;
        cfg.dma_buf_len = 1024;
        cfg.tx_desc_auto_clear = true;
        if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) return false;
        i2s_pin_config_t pins = {};
        pins.mck_io_num = I2S_PIN_NO_CHANGE;
        pins.bck_io_num = BOARD_I2S_BCK;
        pins.ws_io_num = BOARD_I2S_WS;
        pins.data_out_num = BOARD_I2S_DOUT;
        pins.data_in_num = I2S_PIN_NO_CHANGE;
        i2s_set_pin(I2S_NUM_0, &pins);
        s_ready = true;
        s_rate = rate_hz;
    }
    if (rate_hz != s_rate) {
        i2s_set_sample_rates(I2S_NUM_0, rate_hz);
        s_rate = rate_hz;
    }
    return true;
}

size_t audio_write(const int16_t *stereo, size_t frames)
{
    if (!s_ready) return 0;
    size_t written = 0;
    i2s_write(I2S_NUM_0, stereo, frames * 4, &written, 0);
    return written / 4;
}

void audio_silence()
{
    if (s_ready) i2s_zero_dma_buffer(I2S_NUM_0);
}

// ---- tones: a short queue the main loop feeds, so a beep never stalls the UI (#66) ----

struct Tone { uint16_t hz, ms; };
static Tone s_q[12];
static uint8_t s_qh = 0, s_qt = 0;
static uint32_t s_pos = 0;          // frames of the current tone already written
static uint32_t s_last_stream = 0;  // millis() of the last audio_write(), i.e. music playing

void audio_streaming() { s_last_stream = millis(); }

void audio_tone(uint16_t freq_hz, uint16_t ms)
{
    uint8_t next = (s_qt + 1) % 12;
    if (next == s_qh) return;        // queue full: drop it rather than wait
    s_q[s_qt] = {freq_hz, ms};
    s_qt = next;
}

void audio_tick()
{
    if (s_qh == s_qt) return;
    // The music player owns the amplifier while it streams; tones would only garble it
    if (s_last_stream && millis() - s_last_stream < 500) { s_qh = s_qt; s_pos = 0; return; }
    if (!audio_open(s_ready ? s_rate : 16000)) { s_qh = s_qt; return; }
    const uint32_t rate = s_rate;
    const Tone &t = s_q[s_qh];
    const uint32_t total = rate * t.ms / 1000;
    const uint32_t fade = rate / 100; // 10 ms at each end so it doesn't click
    static int16_t buf[128 * 2];
    // A few blocks per tick at most; the DMA takes what fits and the rest waits for the next loop
    for (int block = 0; block < 4 && s_pos < total; block++) {
        uint32_t frames = total - s_pos < 128 ? total - s_pos : 128;
        for (uint32_t i = 0; i < frames; i++) {
            uint32_t n = s_pos + i;
            float a = t.hz ? 6000.0f : 0.0f;
            if (n < fade) a *= (float)n / fade;
            else if (total - n < fade) a *= (float)(total - n) / fade;
            int16_t v = (int16_t)(a * sinf(2.0f * (float)M_PI * t.hz * n / rate));
            buf[2 * i] = buf[2 * i + 1] = v;
        }
        size_t written = audio_write(buf, frames);
        s_pos += written;
        if (written < frames) break;  // DMA full: try again next loop
    }
    if (s_pos >= total) { s_pos = 0; s_qh = (s_qh + 1) % 12; }
}
