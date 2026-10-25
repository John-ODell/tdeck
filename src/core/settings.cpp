#include "settings.h"
#include <Arduino.h>
#include <stddef.h>
#include <Preferences.h>
#include <esp_mac.h>
#include "mesh/crypto.h"

Settings settings;
static const uint32_t SETTINGS_VERSION = 1;

static void defaults()
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    memset(&settings, 0, sizeof(settings));
    snprintf(settings.long_name, sizeof(settings.long_name), "T-Deck %02x%02x", mac[4], mac[5]);
    snprintf(settings.short_name, sizeof(settings.short_name), "%02x%02x", mac[4], mac[5]);
    settings.preset = 0; // LongFast
    settings.psk[0] = 1;
    settings.psk_len = 1;
    settings.tx_power = 22;
    settings.hop_limit = 3;
    settings.rebroadcast = true;
    settings.share_position = false;
    settings.position_precision = 13; // ~1.4 km, Meshtastic's public-channel default
    settings.brightness = 12;
    settings.screen_timeout_s = 10;
    settings.tz_offset_min = -300;
    settings.trackball = 3;
    settings.chat_tone = true;
    settings.tone_volume = 1;
}

void settings_load()
{
    defaults();
    Preferences p;
    p.begin("tdeck", true);
    size_t len = p.getBytesLength("s");
    if (p.getUInt("ver", 0) == SETTINGS_VERSION && len >= offsetof(Settings, trackball) && len <= sizeof(Settings)) {
        p.getBytes("s", &settings, len);
    }
    if (settings.trackball < 1 || settings.trackball > 5) settings.trackball = 3;
    if (settings.tone_volume > 2) settings.tone_volume = 1;
    p.end();
    if (!settings.has_keys) {
        settings.has_keys = crypto::x25519_generate(settings.priv_key, settings.pub_key);
        settings_save();
    }
}

void settings_save()
{
    Preferences p;
    p.begin("tdeck", false);
    p.putUInt("ver", SETTINGS_VERSION);
    p.putBytes("s", &settings, sizeof(Settings));
    p.end();
}

void apply_timezone()
{
    // US zones get their DST rules; anything else is a fixed offset.
    // POSIX TZ offsets are inverted: UTC-5 is "UTC5".
    const char *tz = nullptr;
    switch (settings.tz_offset_min) {
    case -300: tz = "EST5EDT,M3.2.0,M11.1.0"; break;
    case -360: tz = "CST6CDT,M3.2.0,M11.1.0"; break;
    case -420: tz = "MST7MDT,M3.2.0,M11.1.0"; break;
    case -480: tz = "PST8PDT,M3.2.0,M11.1.0"; break;
    case -540: tz = "AKST9AKDT,M3.2.0,M11.1.0"; break;
    case -600: tz = "HST10"; break;
    }
    char buf[24];
    if (!tz) {
        int off = -settings.tz_offset_min;
        snprintf(buf, sizeof(buf), "UTC%c%d:%02d", off < 0 ? '-' : '+', abs(off) / 60, abs(off) % 60);
        tz = buf;
    }
    setenv("TZ", tz, 1);
    tzset();
}
