// Meshtastic over-the-air constants and pure helpers (no hardware access, so
// they can be unit-tested on the host). Values cited in
// research_notes/TDeck Plus Meshtastic firmware/meshtastic_protocol.md.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace mt {

static const uint32_t BROADCAST = 0xFFFFFFFF;
static const size_t HEADER_LEN = 16;
static const size_t MAX_LORA_LEN = 255;
static const size_t PKC_OVERHEAD = 12;    // 8-byte CCM tag + 4-byte extra nonce
static const uint8_t DEFAULT_HOP_LIMIT = 3;
static const uint8_t LORA_SYNC_WORD = 0x2B;
static const uint16_t LORA_PREAMBLE = 16;
static const uint32_t HW_MODEL_T_DECK = 50;

enum Port : uint32_t {
    PORT_UNKNOWN = 0,
    PORT_TEXT = 1,
    PORT_POSITION = 3,
    PORT_NODEINFO = 4,
    PORT_ROUTING = 5,
    PORT_TELEMETRY = 67,
    PORT_TRACEROUTE = 70,
};

static const uint8_t FLAG_HOP_LIMIT_MASK = 0x07;
static const uint8_t FLAG_WANT_ACK = 0x08;
static const uint8_t FLAG_VIA_MQTT = 0x10;
static const uint8_t FLAG_HOP_START_SHIFT = 5;

static const uint8_t DEFAULT_PSK[16] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                        0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};

struct Header {
    uint32_t to, from, id;
    uint8_t flags, channel, next_hop, relay_node;

    uint8_t hop_limit() const { return flags & FLAG_HOP_LIMIT_MASK; }
    uint8_t hop_start() const { return flags >> FLAG_HOP_START_SHIFT; }
    bool want_ack() const { return flags & FLAG_WANT_ACK; }
    void set_hops(uint8_t limit, uint8_t start)
    {
        flags = (flags & ~(FLAG_HOP_LIMIT_MASK | 0xE0)) | (limit & 7) | ((start & 7) << FLAG_HOP_START_SHIFT);
    }
};

inline void put_le32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
inline uint32_t get_le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

inline void header_write(const Header &h, uint8_t *out)
{
    put_le32(out, h.to);
    put_le32(out + 4, h.from);
    put_le32(out + 8, h.id);
    out[12] = h.flags;
    out[13] = h.channel;
    out[14] = h.next_hop;
    out[15] = h.relay_node;
}

inline Header header_read(const uint8_t *in)
{
    Header h;
    h.to = get_le32(in);
    h.from = get_le32(in + 4);
    h.id = get_le32(in + 8);
    h.flags = in[12];
    h.channel = in[13];
    h.next_hop = in[14];
    h.relay_node = in[15];
    return h;
}

// Expand a channel PSK as Channels::getKey() does. Returns key length (0, 16 or 32).
inline size_t expand_psk(const uint8_t *psk, size_t psk_len, uint8_t out[32])
{
    memset(out, 0, 32);
    if (psk_len == 0) return 0;
    if (psk_len == 1) {
        if (psk[0] == 0) return 0;
        memcpy(out, DEFAULT_PSK, 16);
        out[15] = (uint8_t)(out[15] + psk[0] - 1);
        return 16;
    }
    if (psk_len <= 16) { memcpy(out, psk, psk_len); return 16; }
    memcpy(out, psk, psk_len > 32 ? 32 : psk_len);
    return 32;
}

inline uint8_t xor_hash(const uint8_t *p, size_t n)
{
    uint8_t h = 0;
    while (n--) h ^= *p++;
    return h;
}

inline uint8_t channel_hash(const char *name, const uint8_t *key, size_t key_len)
{
    return xor_hash((const uint8_t *)name, strlen(name)) ^ xor_hash(key, key_len);
}

inline uint32_t djb2(const char *s)
{
    uint32_t h = 5381;
    while (*s) h = ((h << 5) + h) + (uint8_t)*s++;
    return h;
}

struct Preset {
    const char *name;
    float bw_khz;
    uint8_t sf, cr;
};

static const Preset PRESETS[] = {
    {"LongFast", 250, 11, 5},
    {"LongSlow", 125, 12, 8},
    {"LongModerate", 125, 11, 8},
    {"MediumSlow", 250, 10, 5},
    {"MediumFast", 250, 9, 5},
    {"ShortSlow", 250, 8, 5},
    {"ShortFast", 250, 7, 5},
    {"ShortTurbo", 500, 7, 5},
};
static const size_t NUM_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

// US 902-928 MHz. Slot = djb2(channel name) % numChannels (0-based).
static const float US_FREQ_START = 902.0f, US_FREQ_END = 928.0f;

inline uint32_t num_slots(float bw_khz)
{
    return (uint32_t)((US_FREQ_END - US_FREQ_START) / (bw_khz / 1000.0f) + 0.5f);
}

inline float slot_freq_mhz(const char *channel_name, float bw_khz, int override_slot = 0)
{
    uint32_t n = num_slots(bw_khz);
    uint32_t slot = override_slot > 0 ? (uint32_t)(override_slot - 1) % n : djb2(channel_name) % n;
    return US_FREQ_START + bw_khz / 2000.0f + slot * (bw_khz / 1000.0f);
}

// AES-CTR IV: id as u64 LE, from as u32 LE, zero. PKI overwrites bytes 4..7 with extra_nonce.
inline void make_nonce(uint8_t nonce[16], uint32_t from, uint32_t id, uint32_t extra_nonce = 0)
{
    memset(nonce, 0, 16);
    put_le32(nonce, id);
    put_le32(nonce + 8, from);
    if (extra_nonce) put_le32(nonce + 4, extra_nonce);
}

// Contention-window slot time in ms for a preset (RadioInterface::slotTimeMsec)
inline float slot_time_ms(float bw_khz, uint8_t sf)
{
    float sym_ms = (float)(1u << sf) / bw_khz;
    return 4.5f * sym_ms + 7.6f;
}

} // namespace mt
