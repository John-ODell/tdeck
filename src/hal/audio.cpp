#include "audio.h"
#include <Arduino.h>
#include <math.h>
#include <driver/i2s.h>
#include "board.h"

static const int RATE = 16000;
static bool s_ready = false;

static bool amp_init()
{
    if (s_ready) return true;
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = 0;
    cfg.dma_buf_count = 4;
    cfg.dma_buf_len = 256;
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
    return true;
}

void audio_beep(uint16_t freq_hz, uint16_t ms)
{
    if (!amp_init()) return;
    // A sine with a short fade at each end so it doesn't click
    static int16_t buf[256 * 2];
    const uint32_t total = (uint32_t)RATE * ms / 1000;
    const uint32_t fade = RATE / 100; // 10 ms
    uint32_t n = 0;
    while (n < total) {
        uint32_t frames = total - n < 256 ? total - n : 256;
        for (uint32_t i = 0; i < frames; i++, n++) {
            float a = 6000.0f;
            if (n < fade) a *= (float)n / fade;
            else if (total - n < fade) a *= (float)(total - n) / fade;
            int16_t v = (int16_t)(a * sinf(2.0f * (float)M_PI * freq_hz * n / RATE));
            buf[2 * i] = buf[2 * i + 1] = v;
        }
        size_t written = 0;
        i2s_write(I2S_NUM_0, buf, frames * 4, &written, portMAX_DELAY);
    }
    i2s_zero_dma_buffer(I2S_NUM_0);
}
