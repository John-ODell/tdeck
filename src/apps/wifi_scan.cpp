#include "apps.h"
#include <Arduino.h>
#include <WiFi.h>
#include <algorithm>
#include "link/link.h"

static lv_obj_t *s_root, *s_list, *s_title;
static bool s_scanning = false;
static uint32_t s_started = 0;

static void start_scan()
{
    dongle::bye(); // the scan takes the radio off the link's channel
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.scanDelete();
    WiFi.scanNetworks(true, true); // async, include hidden
    s_started = millis();
    s_scanning = true;
    lv_label_set_text(s_title, LV_SYMBOL_REFRESH " Scanning...");
}

static const char *auth_name(wifi_auth_mode_t a)
{
    switch (a) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-E";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
    default: return "?";
    }
}

static void show_results(int n)
{
    lv_obj_clean(s_list);
    lv_group_remove_all_objs(ui_group());
    int idx[64];
    int count = n < 64 ? n : 64;
    for (int i = 0; i < count; i++) idx[i] = i;
    std::sort(idx, idx + count, [](int a, int b) { return WiFi.RSSI(a) > WiFi.RSSI(b); });

    int per_channel[15] = {0};
    for (int i = 0; i < n; i++) {
        int ch = WiFi.channel(i);
        if (ch > 0 && ch < 15) per_channel[ch]++;
    }
    char busy[96] = "";
    size_t off = 0;
    for (int ch = 1; ch <= 13; ch++)
        if (per_channel[ch]) off += snprintf(busy + off, sizeof(busy) - off, "%d:%d ", ch, per_channel[ch]);
    lv_label_set_text_fmt(s_title, "%d networks   ch:count %s", n, busy);

    for (int k = 0; k < count; k++) {
        int i = idx[k];
        char line[96];
        String ssid = WiFi.SSID(i);
        // The BSSID gets its own line: after the signal and auth it wrapped in the middle (#79)
        snprintf(line, sizeof(line), "%s\n%d dBm  ch %d  %s\n%s", ssid.length() ? ssid.c_str() : "(hidden)",
                 (int)WiFi.RSSI(i), (int)WiFi.channel(i), auth_name(WiFi.encryptionType(i)), WiFi.BSSIDstr(i).c_str());
        int r = WiFi.RSSI(i);
        lv_obj_t *b = ui_list_btn(s_list, r > -60 ? LV_SYMBOL_WIFI : r > -75 ? LV_SYMBOL_WIFI : LV_SYMBOL_WARNING, line);
        lv_obj_add_event_cb(b, [](lv_event_t *) { start_scan(); }, LV_EVENT_CLICKED, nullptr);
    }
    if (n == 0) ui_list_text(s_list, "Nothing found. Click to rescan.");
}

static bool keys(uint32_t k)
{
    if (k == 'r' || (k == LV_KEY_ENTER && !s_scanning && lv_obj_get_child_cnt(s_list) == 0)) { start_scan(); return true; }
    return false;
}

static void open(lv_obj_t *parent)
{
    s_root = parent;
    s_title = lv_label_create(parent);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_12, 0);
    s_list = lv_list_create(parent);
    lv_obj_set_size(s_list, LV_PCT(100), 196);
    lv_obj_set_y(s_list, 16);
    ui_set_app_key_hook(keys);
    start_scan();
}

static void tick()
{
    if (!s_scanning) return;
    int n = WiFi.scanComplete();
    // Arduino 2.0.9 reports WIFI_SCAN_FAILED once 20x the per-channel time (6 s) has
    // passed, but a slow scan still finishes and its results land a moment later
    if (n == WIFI_SCAN_RUNNING || (n == WIFI_SCAN_FAILED && millis() - s_started < 15000)) return;
    s_scanning = false;
    if (n < 0) {
        lv_obj_clean(s_list);
        lv_label_set_text(s_title, "Scan failed");
        ui_list_text(s_list, "The WiFi scan didn't finish. Press r to retry.");
        return;
    }
    show_results(n);
}

static void close()
{
    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
    s_scanning = false;
}

const App app_wifi = {"WiFi", LV_SYMBOL_WIFI, open, close, tick, nullptr};
