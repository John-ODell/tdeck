// T-Deck <-> USB dongle link over ESP-NOW. Shared by the T-Deck firmware and dongle/.
//
// Pairing: an unpaired dongle broadcasts HELLO with its X25519 public key. The T-Deck
// answers PAIR with its own public key. Both derive the ESP-NOW local master key from
// the shared secret, add each other as encrypted peers, and the dongle confirms with an
// encrypted PAIRED. After that every packet is ESP-NOW encrypted (AES-CCMP) and also
// carries a truncated HMAC, so a packet from anyone without the key is ignored even if
// it spoofs the peer's MAC unencrypted.
//
// Keys go as taps (press + release on the dongle), so a lost packet can't leave a key
// stuck down. Mouse packets carry the full button state.
#pragma once
#include <stdint.h>
#include <string.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

#define LINK_CHANNEL 1
#define LINK_VERSION 1
#define LINK_TAG_LEN 8

// Primary master key: only wraps the per-pair key inside the radio, so a constant is fine
static const uint8_t LINK_PMK[16] = {'t', 'd', 'e', 'c', 'k', '-', 'l', 'i', 'n', 'k', '-', 'p', 'm', 'k', '0', '1'};

enum LinkType : uint8_t {
    LINK_HELLO = 1,  // dongle -> broadcast, plain: LinkKeyMsg
    LINK_PAIR = 2,   // T-Deck -> dongle, plain: LinkKeyMsg
    LINK_PAIRED = 3, // dongle -> T-Deck, encrypted: LinkHdr + tag
    LINK_PING = 4,   // T-Deck -> dongle, encrypted
    LINK_PONG = 5,   // dongle -> T-Deck, encrypted
    LINK_TAP = 10,   // T-Deck -> dongle: LinkTap
    LINK_MOUSE = 11, // T-Deck -> dongle: LinkMouse
};

struct __attribute__((packed)) LinkHdr {
    uint8_t magic[2]; // 'T' 'K'
    uint8_t type;
    uint8_t version;
};

struct __attribute__((packed)) LinkKeyMsg {
    LinkHdr h;
    uint8_t pub[32];
};

struct __attribute__((packed)) LinkCtl {
    LinkHdr h;
    uint8_t tag[LINK_TAG_LEN];
};

struct __attribute__((packed)) LinkTap {
    LinkHdr h;
    uint8_t mod;   // HID modifier bits
    uint8_t usage; // HID keyboard usage
    uint8_t tag[LINK_TAG_LEN];
};

struct __attribute__((packed)) LinkMouse {
    LinkHdr h;
    uint8_t buttons;
    int8_t dx, dy, wheel;
    uint8_t tag[LINK_TAG_LEN];
};

static inline void link_hdr(LinkHdr *h, uint8_t type)
{
    h->magic[0] = 'T';
    h->magic[1] = 'K';
    h->type = type;
    h->version = LINK_VERSION;
}

static inline bool link_hdr_ok(const uint8_t *data, int len, uint8_t type, int size)
{
    const LinkHdr *h = (const LinkHdr *)data;
    return len == size && h->magic[0] == 'T' && h->magic[1] == 'K' && h->type == type && h->version == LINK_VERSION;
}

// ESP-NOW local master key = SHA-256("tdeck-link-v1" || X25519 shared secret)[0..15]
static inline void link_derive_lmk(const uint8_t shared[32], uint8_t lmk[16])
{
    uint8_t buf[13 + 32], out[32];
    memcpy(buf, "tdeck-link-v1", 13);
    memcpy(buf + 13, shared, 32);
    mbedtls_sha256_ret(buf, sizeof(buf), out, 0);
    memcpy(lmk, out, 16);
}

// Tag over everything before the tag field: HMAC-SHA256(lmk)[0..7]
static inline void link_tag(const uint8_t lmk[16], const void *msg, size_t tag_offset, uint8_t tag[LINK_TAG_LEN])
{
    uint8_t out[32];
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), lmk, 16, (const uint8_t *)msg, tag_offset, out);
    memcpy(tag, out, LINK_TAG_LEN);
}

static inline bool link_tag_ok(const uint8_t lmk[16], const void *msg, size_t tag_offset)
{
    uint8_t want[LINK_TAG_LEN];
    link_tag(lmk, msg, tag_offset, want);
    const uint8_t *got = (const uint8_t *)msg + tag_offset;
    uint8_t diff = 0;
    for (int i = 0; i < LINK_TAG_LEN; i++) diff |= want[i] ^ got[i];
    return diff == 0;
}
