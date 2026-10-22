#include "settings.h"
#include <Arduino.h>
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
    settings.screen_timeout_s = 60;
    settings.tz_offset_min = -300;
}

void settings_load()
{
    defaults();
    Preferences p;
    p.begin("tdeck", true);
    if (p.getUInt("ver", 0) == SETTINGS_VERSION && p.getBytesLength("s") == sizeof(Settings)) {
        p.getBytes("s", &settings, sizeof(Settings));
    }
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
