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
    LINK_BLOB = 20,  // either way: one part of a LinkBlob (mesh text, files)
    LINK_ACK = 21,   // answer to a complete blob: LinkAck
};

// Blobs: up to LINK_BLOB_PARTS parts of LINK_BLOB_MAX bytes, all but the last full.
// Every complete blob is answered with an ACK carrying its id.
#define LINK_BLOB_MAX 200
#define LINK_BLOB_PARTS 4

enum LinkBlobKind : uint8_t {
    BLOB_TEXT_EVENT = 1, // T-Deck -> dongle: LinkTextEvent, a mesh message received or sent
    BLOB_SEND_TEXT = 2,  // dongle -> T-Deck: LinkSendText, transmit this on the mesh
    BLOB_FILE_BEGIN = 3, // T-Deck -> dongle: LinkFileBegin
    BLOB_FILE_DATA = 4,  // T-Deck -> dongle: raw bytes, in order
    BLOB_FILE_END = 5,   // T-Deck -> dongle: close the file
    BLOB_SYNC_END = 6,   // T-Deck -> dongle: no more files, show the card to the computer
};

struct __attribute__((packed)) LinkTextEvent {
    uint32_t from, to, id, time;
    uint8_t status, pki;
    char name[40];
    char text[234];
};

struct __attribute__((packed)) LinkSendText {
    uint32_t to; // 0xFFFFFFFF = channel
    char text[234];
};

struct __attribute__((packed)) LinkFileBegin {
    uint32_t size;
    uint32_t mtime; // epoch seconds, 0 if unknown
    char name[64];
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

struct __attribute__((packed)) LinkBlob {
    LinkHdr h;
    uint8_t kind, id, part, parts, len;
    uint8_t data[LINK_BLOB_MAX + LINK_TAG_LEN]; // the tag follows data[len]
};

struct __attribute__((packed)) LinkAck {
    LinkHdr h;
    uint8_t id, ok;
    uint8_t tag[LINK_TAG_LEN];
};

static inline size_t link_blob_size(uint8_t len) { return offsetof(LinkBlob, data) + len + LINK_TAG_LEN; }

// Puts parts back together. Parts of a new id replace whatever was pending.
struct LinkReasm {
    uint8_t id = 0xFF, kind = 0, parts = 0, mask = 0;
    uint16_t len = 0;
    uint8_t buf[LINK_BLOB_MAX * LINK_BLOB_PARTS];

    bool feed(const LinkBlob *b)
    {
        if (b->parts == 0 || b->parts > LINK_BLOB_PARTS || b->part >= b->parts || b->len > LINK_BLOB_MAX) return false;
        if (b->id != id || b->kind != kind || b->parts != parts) {
            id = b->id;
            kind = b->kind;
            parts = b->parts;
            mask = 0;
            len = 0;
        }
        memcpy(buf + b->part * LINK_BLOB_MAX, b->data, b->len);
        mask |= 1 << b->part;
        if (b->part == parts - 1) len = b->part * LINK_BLOB_MAX + b->len;
        return mask == (1 << parts) - 1;
    }
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

// A blob packet is variable length: header + data[len] + tag
static inline bool link_blob_ok(const uint8_t *data, int len)
{
    if (len < (int)link_blob_size(0)) return false;
    const LinkBlob *b = (const LinkBlob *)data;
    return link_hdr_ok(data, len, LINK_BLOB, link_blob_size(b->len));
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
