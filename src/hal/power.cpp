#include "power.h"
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "board.h"
#include "display.h"

static float s_mv = 0;

void power_init()
{
    analogReadResolution(12);
    analogSetPinAttenuation(BOARD_BAT_ADC, ADC_11db);
    s_mv = analogReadMilliVolts(BOARD_BAT_ADC) * 2.0f;
}

uint16_t power_battery_mv()
{
    // Divider is 1:2; low-pass to hide ADC jitter
    float now = analogReadMilliVolts(BOARD_BAT_ADC) * 2.0f;
    s_mv = s_mv * 0.9f + now * 0.1f;
    return (uint16_t)s_mv;
}

uint8_t power_battery_percent()
{
    // Single-cell LiPo resting-voltage curve
    static const uint16_t mv[] = {3300, 3500, 3600, 3700, 3750, 3800, 3850, 3900, 4000, 4100, 4200};
    static const uint8_t pct[] = {0, 5, 10, 20, 30, 40, 50, 60, 75, 90, 100};
    uint16_t v = power_battery_mv();
    if (v <= mv[0]) return 0;
    for (int i = 1; i < 11; i++) {
        if (v <= mv[i]) return pct[i - 1] + (pct[i] - pct[i - 1]) * (v - mv[i - 1]) / (mv[i] - mv[i - 1]);
    }
    return 100;
}

bool power_usb_powered() { return power_battery_mv() > 4250; }

void power_deep_sleep()
{
    display_set_brightness(0);
    digitalWrite(BOARD_POWERON, LOW);
    SPI.end();
    Wire.end();
    esp_sleep_enable_ext1_wakeup(1ull << BOARD_BOOT_PIN, ESP_EXT1_WAKEUP_ALL_LOW);
    esp_deep_sleep_start();
}
