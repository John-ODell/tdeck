#include "apps.h"
#include <Arduino.h>
#include <esp_mac.h>
#include "hal/input.h"
#include "hal/power.h"

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

static lv_obj_t *s_label;

static void refresh()
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    lv_label_set_text_fmt(s_label,
        "T-Deck Plus firmware " FW_VERSION "\n\n"
        "MAC      %02X:%02X:%02X:%02X:%02X:%02X\n"
        "Battery  %u mV (%u%%)\n"
        "Heap     %u KB free\n"
        "PSRAM    %u KB free\n"
        "Keyboard %s   Touch %s\n"
        "Uptime   %lu s",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
        power_battery_mv(), power_battery_percent(),
        (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getFreePsram() / 1024),
        input_keyboard_found() ? "yes" : "no", input_touch_found() ? "yes" : "no",
        (unsigned long)(millis() / 1000));
}

static void open(lv_obj_t *parent)
{
    s_label = lv_label_create(parent);
    lv_obj_set_style_text_font(s_label, &lv_font_montserrat_14, 0); // the theme's mono font is too wide for these lines
    lv_obj_set_width(s_label, LV_PCT(100));
    lv_label_set_long_mode(s_label, LV_LABEL_LONG_WRAP);
    refresh();
}

static void tick()
{
    static uint32_t last = 0;
    if (millis() - last > 1000) { last = millis(); refresh(); }
}

const App app_about = {"About", LV_SYMBOL_HOME, open, nullptr, tick, nullptr};
