#include "board.h"
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <driver/gpio.h>
#include <esp_chip_info.h>

static SemaphoreHandle_t s_spi_mutex;
static BoardRev s_rev = BOARD_REV_NA;

bool board_spi_lock(uint32_t timeout_ms)
{
    if (!s_spi_mutex) return true;  // before board_early_init: single-threaded
    TickType_t t = timeout_ms == 0xFFFFFFFFu ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_spi_mutex, t) == pdTRUE;
}

void board_spi_unlock()
{
    if (s_spi_mutex) xSemaphoreGiveRecursive(s_spi_mutex);
}

static bool i2c_present(uint8_t addr)
{
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

static void out_low(int pin) { pinMode(pin, OUTPUT); digitalWrite(pin, LOW); }
static void out_high(int pin) { pinMode(pin, OUTPUT); digitalWrite(pin, HIGH); }

#if defined(BOARD_TDECK_PRO)

static const LoraPins LORA = {RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN,
                              BOARD_LORA_EN, 2.4f, true};

BoardModel board_model() { return BOARD_MODEL_TDECK_PRO; }
const char *board_name() { return "T-Deck Pro"; }

void board_early_init()
{
    if (!s_spi_mutex) s_spi_mutex = xSemaphoreCreateRecursiveMutex();

    // Pins held through deep sleep stay held after wake until released
    const gpio_num_t held[] = {(gpio_num_t)BOARD_MODEM_BOOST_EN, (gpio_num_t)BOARD_LORA_EN,
                               (gpio_num_t)BOARD_GPS_EN, (gpio_num_t)BOARD_MODEM_PWRKEY,
                               (gpio_num_t)BOARD_HAPTIC_EN, (gpio_num_t)BOARD_KB_BACKLIGHT};
    for (gpio_num_t g : held) gpio_hold_dis(g);
    gpio_deep_sleep_hold_dis();

    // Rails default off; each service powers its own part. The factory firmware drives
    // all of these HIGH and leaves the modem's PWRKEY pressed.
    out_low(BOARD_MODEM_PWRKEY);
    out_low(BOARD_MODEM_BOOST_EN);
    out_low(BOARD_GPS_EN);
    out_low(BOARD_HAPTIC_EN);
    out_low(BOARD_KB_BACKLIGHT);
    out_high(BOARD_LORA_EN);    // the mesh is a core service

    // Park every chip select on the shared bus before it starts
    out_high(RADIO_CS_PIN);
    out_high(BOARD_EPD_CS);
    out_high(BOARD_SDCARD_CS);
    out_high(RADIO_RST_PIN);
    pinMode(BOARD_SPI_MISO, INPUT_PULLUP);
    SPI.begin(BOARD_SPI_SCK, BOARD_SPI_MISO, BOARD_SPI_MOSI);

    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_HZ);

    // Revision. GPIO38 HIGH is safe on both: the 1.8 V rail on V1.0, touch out of reset on
    // V1.1. Look for V1.0's light sensor first, so a V1.0 board never has its motor driven.
    out_high(BOARD_PIN38);
    delay(10);
    bool ltr = i2c_present(0x23);
    bool drv = false;
    if (!ltr) {
        digitalWrite(BOARD_HAPTIC_EN, HIGH);  // V1.1: the DRV2605L only answers when powered
        delay(5);
        for (int i = 0; i < 3 && !drv; i++) { drv = i2c_present(0x5A); if (!drv) delay(5); }
        digitalWrite(BOARD_HAPTIC_EN, LOW);
    }
    s_rev = board_rev_from_probe(ltr, drv);

    if (s_rev == BOARD_REV_PRO_V1_1) {
        out_low(BOARD_PIN45);                    // front light off
        out_low(BOARD_PIN16);                    // e-paper reset pulse; the image stays
        delay(10);
        digitalWrite(BOARD_PIN16, HIGH);
        digitalWrite(BOARD_PIN38, LOW);          // touch reset pulse
        delay(5);
        digitalWrite(BOARD_PIN38, HIGH);
        delay(50);
    } else if (s_rev == BOARD_REV_PRO_V1_0) {
        pinMode(BOARD_PIN16, INPUT);             // LTR-553 interrupt
        out_low(BOARD_PIN45);                    // touch reset pulse
        delay(5);
        digitalWrite(BOARD_PIN45, HIGH);
        delay(50);
    }
    // Unknown revision: leave 16 and 45 alone; the banner says so.
}

#else  // T-Deck Plus

static const LoraPins LORA = {RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN,
                              -1, 1.8f, true};

BoardModel board_model() { return BOARD_MODEL_TDECK_PLUS; }
const char *board_name() { return "T-Deck Plus"; }

void board_early_init()
{
    if (!s_spi_mutex) s_spi_mutex = xSemaphoreCreateRecursiveMutex();

    // Peripheral rail first, then park every SPI chip select before the bus starts
    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);
    pinMode(BOARD_SDCARD_CS, OUTPUT);
    pinMode(RADIO_CS_PIN, OUTPUT);
    pinMode(BOARD_TFT_CS, OUTPUT);
    digitalWrite(BOARD_SDCARD_CS, HIGH);
    digitalWrite(RADIO_CS_PIN, HIGH);
    digitalWrite(BOARD_TFT_CS, HIGH);
    pinMode(BOARD_SPI_MISO, INPUT_PULLUP);
    SPI.begin(BOARD_SPI_SCK, BOARD_SPI_MISO, BOARD_SPI_MOSI);

    // Keyboard MCU needs a moment after the rail comes up
    delay(200);
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
}

#endif

BoardRev board_rev() { return s_rev; }
uint32_t board_caps() { return board_caps_for(board_model(), s_rev); }
const LoraPins &board_lora_pins() { return LORA; }

static const char *i2c_name(uint8_t a)
{
    if (board_model() == BOARD_MODEL_TDECK_PRO) {
        switch (a) {
        case 0x1A: return "touch (Hynitron)";
        case 0x23: return "LTR-553 light";
        case 0x28: return "BHI260AP IMU";
        case 0x34: return "TCA8418 keyboard";
        case 0x55: return "BQ27220 gauge";
        case 0x5A: return "DRV2605L haptics";
        case 0x6B: return "BQ25896 charger";
        }
    } else {
        switch (a) {
        case 0x14: case 0x5D: return "GT911 touch";
        case 0x55: return "keyboard MCU";
        }
    }
    return "?";
}

static const char *const CAP_NAMES[] = {
    "color-lcd", "epaper", "backlight", "trackball", "touch", "i2s-speaker", "haptics",
    "fuel-gauge", "charger-i2c", "imu", "modem", "gps", "lora", "keyboard", "sd",
    "battery-adc", "light-sensor"};

void board_print_banner(Print &out)
{
    esp_chip_info_t ci;
    esp_chip_info(&ci);
    out.printf("[BOARD] %s rev %s\n", board_name(), board_rev_name(s_rev));
    out.printf("[BOARD] ESP32-S3 rev %d, %u MHz, flash %u MB, PSRAM %u bytes\n", ci.revision,
               getCpuFrequencyMhz(), ESP.getFlashChipSize() / (1024 * 1024), ESP.getPsramSize());
    out.print("[BOARD] caps:");
    uint32_t caps = board_caps();
    for (unsigned i = 0; i < sizeof(CAP_NAMES) / sizeof(CAP_NAMES[0]); i++)
        if (caps & (1u << i)) out.printf(" %s", CAP_NAMES[i]);
    out.println();
    for (uint8_t a = 1; a < 0x7F; a++)
        if (i2c_present(a)) out.printf("[BOARD] I2C 0x%02X %s\n", a, i2c_name(a));
    const LoraPins &l = board_lora_pins();
    out.printf("[BOARD] LoRa CS %d DIO1 %d RST %d BUSY %d EN %d TCXO %.1f V\n", l.cs, l.dio1, l.rst,
               l.busy, l.power_en, l.tcxo_v);
}
