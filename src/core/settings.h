#pragma once
#include <stdint.h>
#include <stddef.h>

struct Settings {
    char long_name[40];
    char short_name[5];
    uint8_t preset;          // index into mt::PRESETS
    char channel_name[12];   // "" = preset name
    uint8_t psk[32];
    uint8_t psk_len;         // 1 with psk[0]=1 is the default public key
    int8_t tx_power;         // dBm, SX1262 max 22
    uint8_t hop_limit;
    bool rebroadcast;        // CLIENT role relays
    bool share_position;
    uint8_t position_precision; // bits, 32 = exact
    uint8_t brightness;      // 1..16
    uint16_t screen_timeout_s;
    int16_t tz_offset_min;
    uint8_t priv_key[32];
    uint8_t pub_key[32];
    bool has_keys;
};

extern Settings settings;
void settings_load();
void settings_save();
