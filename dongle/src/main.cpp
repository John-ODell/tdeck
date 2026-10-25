// USB keyboard + mouse that types what the T-Deck sends over ESP-NOW.
//
// Unpaired (or BOOT held for 2 s) it broadcasts HELLO until a T-Deck answers PAIR from
// the Keyboard app. The new pairing is only saved once the T-Deck's first encrypted
// packet checks out, so a pairing that half-failed leaves the old one in place.
//
// Besides keys, the link carries mesh chat both ways (JSON lines on the USB serial port)
// and notes the T-Deck pushes to the TF card, which the computer sees as a USB drive.
//
// LED: fast blink = pairing, slow blink = paired but the T-Deck is away, on = connected.
// Boards with a screen (GEEK) show the same plus the last keys typed.
#include <Arduino.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <mbedtls/ecp.h>
#include <Preferences.h>
#include "link_proto.h"
#ifdef GEEK_LCD
#include <TFT_eSPI.h>
#endif
#ifdef GEEK_SD
#include <USBMSC.h>
#include <SD_MMC.h>
#include <sdmmc_cmd.h>
#include <utime.h>
#include "tusb.h"
#endif

// Serial is the USB CDC port, started from setup() instead of at boot so the USB identity
// is in place before the computer enumerates the device (#55)
#if !ARDUINO_USB_CDC_ON_BOOT
static USBCDC USBSerial;
#define Serial USBSerial
#endif

static USBHIDKeyboard Keyboard;
static USBHIDMouse Mouse;

static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const int BOOT_PIN = 0;

static bool s_paired = false;
static uint8_t s_mac[6], s_lmk[16];

// Pairing mode: our keypair, then the pairing on trial until the T-Deck proves the key
static bool s_pairing = false;
static uint8_t s_priv[32], s_pub[32];
static bool s_trial = false;
static uint8_t s_trial_mac[6], s_trial_lmk[16];
static uint32_t s_trial_at = 0, s_paired_sent_at = 0;
static int s_paired_sends = 0;

static uint32_t s_last_rx = 0, s_last_hello = 0;
static uint8_t s_buttons = 0;
static uint32_t s_taps = 0;
static char s_last[24] = "";
static LinkReasm s_reasm;
static uint8_t s_blob_id = 0;
static volatile int16_t s_ack_id = -1;
static volatile bool s_ack_ok = false;

// Serving the card to the T-Deck: one job at a time, one blob in flight, each ACKed
enum Job { JOB_NONE, JOB_LIST, JOB_GET };
static Job s_job = JOB_NONE;
static File s_job_dir, s_job_file;
static uint8_t s_job_buf[LINK_BLOB_MAX * LINK_BLOB_PARTS];
static size_t s_job_len = 0;
static uint8_t s_job_kind = 0, s_job_wait = 0;
static bool s_job_waiting = false, s_job_last = false;
static uint32_t s_job_sent_at = 0;
static int s_job_retries = 0;
static struct { bool has, dir; uint32_t size; uint8_t nlen; char name[48]; } s_carry;
static char s_last_msg[64] = "";
static uint32_t s_msg_ver = 0;
static char s_line[300];
static size_t s_line_len = 0;

// ESP-NOW callbacks run in the WiFi task; USB work happens in loop()
struct Rx { uint8_t mac[6]; uint8_t len; uint8_t data[250]; };
static QueueHandle_t s_rxq;

static int rng(void *, unsigned char *out, size_t n)
{
    esp_fill_random(out, n);
    return 0;
}

// X25519 via mbedtls (same as the T-Deck's src/mesh/crypto.cpp)
static bool x25519(const uint8_t scalar[32], const uint8_t *point /* null = base */, uint8_t out[32])
{
    mbedtls_ecp_group grp;
    mbedtls_mpi d;
    mbedtls_ecp_point P, R;
    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&d);
    mbedtls_ecp_point_init(&P);
    mbedtls_ecp_point_init(&R);
    bool ok = false;
    uint8_t k[32];
    memcpy(k, scalar, 32);
    k[0] &= 248;
    k[31] &= 127;
    k[31] |= 64;
    do {
        if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519)) break;
        if (mbedtls_mpi_read_binary_le(&d, k, 32)) break;
        if (point) {
            uint8_t u[32];
            memcpy(u, point, 32);
            u[31] &= 127;
            if (mbedtls_mpi_read_binary_le(&P.X, u, 32)) break;
            if (mbedtls_mpi_lset(&P.Z, 1)) break;
        } else if (mbedtls_ecp_copy(&P, &grp.G)) {
            break;
        }
        if (mbedtls_ecp_mul(&grp, &R, &d, &P, rng, nullptr)) break;
        if (mbedtls_mpi_write_binary_le(&R.X, out, 32)) break;
        ok = true;
    } while (0);
    memset(k, 0, sizeof(k));
    mbedtls_ecp_point_free(&R);
    mbedtls_ecp_point_free(&P);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_group_free(&grp);
    return ok;
}

static const char *state_name()
{
    if (s_pairing) return "pairing";
    if (s_paired && s_last_rx && millis() - s_last_rx < 3000) return "online";
    return "away";
}

static void led(bool on)
{
#if defined(LED_RGB_PIN)
    neopixelWrite(LED_RGB_PIN, 0, on ? 24 : 0, 0);
#elif defined(LED_PIN)
    digitalWrite(LED_PIN, on);
#endif
}

#ifdef GEEK_LCD
static TFT_eSPI tft;
enum ScreenState { SCR_PAIRING, SCR_AWAY, SCR_ONLINE };
static int s_scr_state = -1;
static uint32_t s_scr_taps = (uint32_t)-1, s_scr_msg_ver = (uint32_t)-1;

static void screen_init()
{
    tft.init();
    tft.setRotation(1); // USB plug on the left, 240 wide
    tft.fillScreen(TFT_BLACK);
}

// Dark background, phosphor green text, like the T-Deck
static void screen_draw(ScreenState st)
{
    const uint16_t green = tft.color565(0x00, 0xff, 0x41), dim = tft.color565(0x1c, 0x8c, 0x3c);
    const uint16_t amber = tft.color565(0xff, 0xb0, 0x30);
    if (st != s_scr_state) {
        s_scr_state = st;
        s_scr_taps = (uint32_t)-1;
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(dim, TFT_BLACK);
        tft.drawString("T-DECK DONGLE", 6, 4, 2);
        tft.setTextColor(st == SCR_ONLINE ? green : amber, TFT_BLACK);
        switch (st) {
        case SCR_PAIRING:
            tft.drawString("Pairing", 6, 28, 4);
            tft.setTextColor(dim, TFT_BLACK);
            tft.drawString("On the T-Deck: Keyboard >", 6, 62, 2);
            tft.drawString("Pair a dongle", 6, 80, 2);
            break;
        case SCR_AWAY:
            tft.drawString("T-Deck away", 6, 28, 4);
            tft.setTextColor(dim, TFT_BLACK);
            tft.drawString("Hold BOOT 2 s to pair again", 6, 62, 2);
            break;
        case SCR_ONLINE:
            tft.drawString("Connected", 6, 28, 4);
            s_scr_msg_ver = (uint32_t)-1;
            break;
        }
        if (s_paired) {
            char mac[32];
            snprintf(mac, sizeof(mac), "T-Deck %02x:%02x:%02x", s_mac[3], s_mac[4], s_mac[5]);
            tft.setTextColor(dim, TFT_BLACK);
            tft.drawString(mac, 6, 116, 2);
        }
    }
    if (st == SCR_ONLINE && s_taps != s_scr_taps) {
        s_scr_taps = s_taps;
        char line[48];
        snprintf(line, sizeof(line), "%lu keys  %s", (unsigned long)s_taps, s_last);
        tft.setTextColor(green, TFT_BLACK);
        tft.setTextPadding(228);
        tft.drawString(line, 6, 80, 2);
        tft.setTextPadding(0);
    }
    if (st == SCR_ONLINE && s_msg_ver != s_scr_msg_ver) {
        s_scr_msg_ver = s_msg_ver;
        char line[40];
        strlcpy(line, s_last_msg, sizeof(line));
        tft.setTextColor(dim, TFT_BLACK);
        tft.setTextPadding(228);
        tft.drawString(line, 6, 98, 2);
        tft.setTextPadding(0);
    }
}
#endif

// Printable form of a tap for the screen
static void note_tap(uint8_t mod, uint8_t usage)
{
    s_taps++;
    const char *name = nullptr;
    char c = 0;
    if (usage >= 0x04 && usage <= 0x1D) c = (mod & 0x02 ? 'A' : 'a') + usage - 0x04;
    else if (usage >= 0x1E && usage <= 0x26) c = '1' + usage - 0x1E;
    else if (usage == 0x27) c = '0';
    else if (usage == 0x2C) name = "space";
    else if (usage == 0x28) name = "enter";
    else if (usage == 0x2A) name = "bksp";
    else if (usage == 0x2B) name = "tab";
    else if (usage == 0x29) name = "esc";
    else if (usage >= 0x4F && usage <= 0x52) name = "arrow";
    size_t n = strlen(s_last);
    if (name) {
        snprintf(s_last, sizeof(s_last), "%s", name);
    } else if (c) {
        if (n >= 12) { memmove(s_last, s_last + 1, n); n--; }
        s_last[n] = c;
        s_last[n + 1] = 0;
    }
}

// ---- JSON on the serial port ------------------------------------------------------

static void json_str(const char *s)
{
    Serial.print('"');
    for (; *s; s++) {
        uint8_t c = *s;
        if (c == '"' || c == '\\') { Serial.print('\\'); Serial.print((char)c); }
        else if (c == '\n') Serial.print("\\n");
        else if (c < 0x20) Serial.printf("\\u%04x", c);
        else Serial.print((char)c);
    }
    Serial.print('"');
}

static const char *state_name();
static void json_link()
{
    Serial.printf("{\"type\":\"link\",\"state\":\"%s\"", state_name());
    if (s_paired) Serial.printf(",\"tdeck\":\"%02x:%02x:%02x:%02x:%02x:%02x\"", s_mac[0], s_mac[1], s_mac[2], s_mac[3], s_mac[4], s_mac[5]);
    Serial.println("}");
}

// Finds "key":"value" in a one-line JSON object; good enough for what the host sends
static bool json_get(const char *line, const char *key, char *out, size_t cap)
{
    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(line, pat);
    if (!p) return false;
    p = strchr(p + strlen(pat), ':');
    if (!p) return false;
    while (*++p == ' ') {}
    if (*p != '"') return false;
    size_t n = 0;
    for (p++; *p && *p != '"' && n + 1 < cap; p++) {
        if (*p == '\\' && p[1]) {
            p++;
            out[n++] = *p == 'n' ? '\n' : *p;
        } else {
            out[n++] = *p;
        }
    }
    out[n] = 0;
    return true;
}

// ---- blobs ------------------------------------------------------------------------

static void send_ack(uint8_t id, bool ok)
{
    LinkAck a;
    link_hdr(&a.h, LINK_ACK);
    a.id = id;
    a.ok = ok;
    link_tag(s_lmk, &a, offsetof(LinkAck, tag), a.tag);
    esp_now_send(s_mac, (const uint8_t *)&a, sizeof(a));
}

static bool send_blob(uint8_t kind, const void *data, size_t len, uint8_t *id_out = nullptr)
{
    if (!s_paired || len == 0 || len > LINK_BLOB_MAX * LINK_BLOB_PARTS) return false;
    uint8_t id = ++s_blob_id;
    if (id_out) *id_out = id;
    uint8_t parts = (len + LINK_BLOB_MAX - 1) / LINK_BLOB_MAX;
    for (uint8_t p = 0; p < parts; p++) {
        LinkBlob b;
        link_hdr(&b.h, LINK_BLOB);
        b.kind = kind;
        b.id = id;
        b.part = p;
        b.parts = parts;
        size_t off = p * LINK_BLOB_MAX;
        b.len = len - off > LINK_BLOB_MAX ? LINK_BLOB_MAX : len - off;
        memcpy(b.data, (const uint8_t *)data + off, b.len);
        link_tag(s_lmk, &b, offsetof(LinkBlob, data) + b.len, b.data + b.len);
        if (esp_now_send(s_mac, (const uint8_t *)&b, link_blob_size(b.len)) != ESP_OK) return false;
    }
    return true;
}

// ---- TF card as a USB drive (GEEK) ------------------------------------------------
// The card belongs to the computer except while the T-Deck is writing notes to it; then
// it is pulled (the computer sees it removed), remounted here, written, and given back.
#ifdef GEEK_SD
static USBMSC s_msc;
static bool s_sd = false, s_presented = false;
static File s_sd_file;
static uint32_t s_sd_busy_at = 0;
static char s_sd_path[96];
static uint32_t s_sd_mtime = 0;
static volatile bool s_host_wrote = false; // the computer wrote since we last looked

struct SDAccess : public fs::SDMMCFS {
    sdmmc_card_t *card() { return _card; }
};
static sdmmc_card_t *sd_card() { return static_cast<SDAccess &>(SD_MMC).card(); }

static int32_t msc_read(uint32_t lba, uint32_t off, void *buf, uint32_t n)
{
    sdmmc_card_t *c = sd_card();
    if (!c || off || !s_presented) return -1;
    return sdmmc_read_sectors(c, buf, lba, n / 512) == ESP_OK ? (int32_t)n : -1;
}

static int32_t msc_write(uint32_t lba, uint32_t off, uint8_t *buf, uint32_t n)
{
    sdmmc_card_t *c = sd_card();
    if (!c || off || !s_presented) return -1;
    s_host_wrote = true;
    return sdmmc_write_sectors(c, buf, lba, n / 512) == ESP_OK ? (int32_t)n : -1;
}

static bool sd_begin()
{
    SD_MMC.setPins(36, 35, 37, 33, 38, 34);
    s_sd = SD_MMC.begin("/sd", false, false, 20000);
    return s_sd;
}

// Before USB.begin(), so the drive is part of the composite device
static void sd_init()
{
    if (!sd_begin()) {
        Serial.println("[dongle] no TF card; notes sync is off");
        return;
    }
    s_msc.vendorID("tdeck");
    s_msc.productID("T-Deck notes");
    s_msc.productRevision("1.0");
    s_msc.onRead(msc_read);
    s_msc.onWrite(msc_write);
    s_msc.mediaPresent(true);
    s_presented = true;
    s_msc.begin(SD_MMC.cardSize() / 512, 512);
    Serial.printf("[dongle] TF card %llu MB as a USB drive\n", SD_MMC.cardSize() >> 20);
}

static bool sd_take()
{
    if (!s_sd) return false;
    s_sd_busy_at = millis();
    if (!s_presented) return true;
    s_msc.mediaPresent(false);
    s_presented = false;
    delay(50);
    SD_MMC.end(); // drop anything cached from before the computer wrote to it
    if (!sd_begin()) return false;
    SD_MMC.mkdir("/tdeck");
    SD_MMC.mkdir("/tdeck/notes");
    return true;
}

// Flagging the medium as changed isn't enough for macOS to re-read it, so the dongle
// unplugs itself from USB for a moment. The keyboard comes back with it.
static void sd_give()
{
    if (!s_sd || s_presented) return;
    if (s_sd_file) s_sd_file.close();
    s_msc.mediaPresent(true);
    s_presented = true;
    tud_disconnect();
    delay(300);
    tud_connect();
}

// Reads happen while the computer has the drive too; both only read the card then. The
// card is remounted first if the computer wrote to it, so the listing isn't stale.
static bool sd_reading()
{
    if (!s_sd) return false;
    if (s_presented && s_host_wrote) {
        s_host_wrote = false;
        s_msc.mediaPresent(false);
        delay(20);
        SD_MMC.end();
        bool ok = sd_begin();
        s_msc.mediaPresent(true);
        if (!ok) return false;
    }
    return true;
}

// "a/b/c.txt" relative to the card root -> "/a/b/c.txt", no "..", folders created if asked
static bool sd_path(const char *rel, char *out, size_t cap, bool make_dirs)
{
    if (strstr(rel, "..")) return false;
    while (*rel == '/') rel++;
    snprintf(out, cap, "/%s", rel);
    size_t n = strlen(out);
    while (n > 1 && out[n - 1] == '/') out[--n] = 0;
    if (make_dirs)
        for (size_t i = 1; i < n; i++)
            if (out[i] == '/') { out[i] = 0; SD_MMC.mkdir(out); out[i] = '/'; }
    return true;
}

static bool sd_file_begin(const LinkFileBegin *b)
{
    if (!sd_take()) return false;
    if (s_sd_file) s_sd_file.close();
    char path[96];
    if (!sd_path(b->name, path, sizeof(path), true)) return false;
    snprintf(s_sd_path, sizeof(s_sd_path), "/sd%s", path);
    s_sd_mtime = b->mtime;
    s_sd_file = SD_MMC.open(path, FILE_WRITE);
    Serial.printf("{\"type\":\"file\",\"name\":\"%s\",\"size\":%lu}\n", path + 1, (unsigned long)b->size);
    return (bool)s_sd_file;
}

static void job_end()
{
    if (s_job_dir) s_job_dir.close();
    if (s_job_file) s_job_file.close();
    s_job = JOB_NONE;
    s_job_waiting = false;
}

static void job_error(const char *msg)
{
    send_blob(BLOB_FS_ERROR, msg, strlen(msg));
    job_end();
}

static bool job_send(uint8_t kind, size_t len)
{
    s_job_kind = kind;
    s_job_len = len;
    s_job_retries = 0;
    if (!send_blob(kind, s_job_buf, len, &s_job_wait)) { job_end(); return false; }
    s_job_sent_at = millis();
    s_job_waiting = true;
    return true;
}

static void job_start_list(const char *rel)
{
    if (s_job != JOB_NONE) job_end();
    char path[96];
    if (!sd_reading() || !sd_path(rel, path, sizeof(path), false)) { job_error("no card"); return; }
    s_job_dir = SD_MMC.open(path);
    if (!s_job_dir || !s_job_dir.isDirectory()) { job_error("no such folder"); return; }
    s_job = JOB_LIST;
    s_job_last = false;
    s_carry.has = false;
}

static void job_start_get(const char *rel)
{
    if (s_job != JOB_NONE) job_end();
    char path[96];
    if (!sd_reading() || !sd_path(rel, path, sizeof(path), false)) { job_error("no card"); return; }
    s_job_file = SD_MMC.open(path, FILE_READ);
    if (!s_job_file || s_job_file.isDirectory()) { job_error("no such file"); return; }
    s_job = JOB_GET;
    LinkFileBegin b = {};
    b.size = s_job_file.size();
    b.mtime = s_job_file.getLastWrite();
    strlcpy(b.name, path + 1, sizeof(b.name));
    memcpy(s_job_buf, &b, sizeof(b));
    job_send(BLOB_FILE_BEGIN, sizeof(b));
}

// Next blob of the running job; called when nothing is in flight
static void job_step()
{
    if (s_job == JOB_LIST) {
        if (s_job_last) { job_end(); return; }
        LinkFsEntries *h = (LinkFsEntries *)s_job_buf;
        h->count = 0;
        h->last = 0;
        size_t n = sizeof(*h);
        auto put = [&](uint32_t size, bool dir, const char *name, uint8_t nl) {
            LinkFsEntry *e = (LinkFsEntry *)(s_job_buf + n);
            e->size = size;
            e->dir = dir;
            e->nlen = nl;
            memcpy(s_job_buf + n + sizeof(*e), name, nl);
            n += sizeof(*e) + nl;
            h->count++;
        };
        if (s_carry.has) { put(s_carry.size, s_carry.dir, s_carry.name, s_carry.nlen); s_carry.has = false; }
        for (;;) {
            File f = s_job_dir.openNextFile();
            if (!f) { h->last = 1; break; }
            const char *name = f.name();
            const char *slash = strrchr(name, '/');
            if (slash) name = slash + 1;
            if (name[0] == '.') { f.close(); continue; } // macOS's ._ shadows and Spotlight index
            uint8_t nl = strlen(name) > 47 ? 47 : strlen(name);
            if (n + sizeof(LinkFsEntry) + nl > sizeof(s_job_buf) || h->count == 255) {
                // Full: this entry opens the next batch (a File can't be stepped back)
                s_carry.has = true;
                s_carry.size = f.isDirectory() ? 0 : f.size();
                s_carry.dir = f.isDirectory();
                s_carry.nlen = nl;
                memcpy(s_carry.name, name, nl);
                f.close();
                break;
            }
            put(f.isDirectory() ? 0 : f.size(), f.isDirectory(), name, nl);
            f.close();
        }
        s_job_last = h->last;
        job_send(BLOB_FS_ENTRIES, n);
        return;
    }
    if (s_job == JOB_GET) {
        if (s_job_kind == BLOB_FILE_END) { job_end(); return; }
        size_t n = s_job_file.read(s_job_buf, sizeof(s_job_buf));
        if (n == 0) { s_job_buf[0] = 0; job_send(BLOB_FILE_END, 1); return; }
        job_send(BLOB_FILE_DATA, n);
    }
}

static void job_tick(uint32_t now)
{
    if (s_job == JOB_NONE) return;
    if (!s_job_waiting) { job_step(); return; }
    if (s_ack_id == s_job_wait) {
        bool ok = s_ack_ok;
        s_ack_id = -1;
        s_job_waiting = false;
        if (!ok) { job_end(); return; }
        job_step();
        return;
    }
    if (now - s_job_sent_at > 1500) {
        if (++s_job_retries > 3) { job_end(); return; }
        send_blob(s_job_kind, s_job_buf, s_job_len, &s_job_wait);
        s_job_sent_at = now;
    }
}

static bool sd_file_data(const uint8_t *d, size_t n)
{
    if (!s_sd_file) return false;
    s_sd_busy_at = millis();
    return s_sd_file.write(d, n) == n;
}

static void sd_file_end()
{
    if (!s_sd_file) return;
    s_sd_file.close();
    if (s_sd_mtime) {
        struct utimbuf t = {(time_t)s_sd_mtime, (time_t)s_sd_mtime};
        utime(s_sd_path, &t);
    }
}

static void sd_tick(uint32_t now)
{
    // A sync that stops half way must not keep the drive from the computer
    if (s_sd && !s_presented && now - s_sd_busy_at > 5000) sd_give();
}
#endif

static bool on_blob(uint8_t kind, const uint8_t *data, size_t len)
{
    switch (kind) {
    case BLOB_TEXT_EVENT: {
        if (len < offsetof(LinkTextEvent, text) + 1) return false;
        LinkTextEvent e = {};
        memcpy(&e, data, len > sizeof(e) ? sizeof(e) : len);
        e.name[sizeof(e.name) - 1] = 0;
        e.text[sizeof(e.text) - 1] = 0;
        Serial.print("{\"type\":\"text\",\"from\":\"");
        Serial.printf("%08lx\",\"to\":\"%08lx\",\"id\":%lu,\"time\":%lu,\"status\":%u,\"dm\":%s,\"name\":",
                      (unsigned long)e.from, (unsigned long)e.to, (unsigned long)e.id, (unsigned long)e.time, e.status,
                      e.to == 0xFFFFFFFF ? "false" : "true");
        json_str(e.name);
        Serial.print(",\"text\":");
        json_str(e.text);
        Serial.println("}");
        snprintf(s_last_msg, sizeof(s_last_msg), "%s: %s", e.name, e.text);
        s_msg_ver++;
        return true;
    }
#ifdef GEEK_SD
    case BLOB_FILE_BEGIN:
        return len >= sizeof(LinkFileBegin) && sd_file_begin((const LinkFileBegin *)data);
    case BLOB_FILE_DATA:
        return sd_file_data(data, len);
    case BLOB_FILE_END:
        sd_file_end();
        return true;
    case BLOB_SYNC_END:
        sd_give();
        Serial.println("{\"type\":\"sync\",\"state\":\"done\"}");
        return true;
    case BLOB_FS_LIST: case BLOB_FS_GET: {
        char rel[96];
        size_t n = len < sizeof(rel) - 1 ? len : sizeof(rel) - 1;
        memcpy(rel, data, n);
        rel[n] = 0;
        if (kind == BLOB_FS_LIST) job_start_list(rel); else job_start_get(rel);
        return true;
    }
#else
    case BLOB_FILE_BEGIN: case BLOB_FILE_DATA: case BLOB_FILE_END: case BLOB_SYNC_END:
    case BLOB_FS_LIST: case BLOB_FS_GET:
        return false; // no card on this board
#endif
    }
    return false;
}

// A line from the host: {"send":"text"} with an optional "to" node hex, or plain text
static void host_line(char *line)
{
    while (*line == ' ') line++;
    if (!*line) return;
    LinkSendText t = {};
    t.to = 0xFFFFFFFF;
    char to[12];
    if (*line == '{') {
        if (strstr(line, "\"get\"")) { json_link(); return; }   // {"get":"link"}: state on demand
        if (!json_get(line, "send", t.text, sizeof(t.text))) return;
        if (json_get(line, "to", to, sizeof(to))) t.to = strtoul(to, nullptr, 16);
    } else {
        strlcpy(t.text, line, sizeof(t.text));
    }
    if (!t.text[0]) return;
    bool online = s_paired && !s_pairing && s_last_rx && millis() - s_last_rx < 3000;
    if (!online || !send_blob(BLOB_SEND_TEXT, &t, sizeof(uint32_t) + strlen(t.text) + 1))
        Serial.println("{\"type\":\"error\",\"text\":\"T-Deck not connected\"}");
}

static void add_peer(const uint8_t mac[6], const uint8_t *lmk)
{
    esp_now_del_peer(mac);
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, mac, 6);
    p.channel = LINK_CHANNEL;
    p.ifidx = WIFI_IF_STA;
    p.encrypt = lmk != nullptr;
    if (lmk) memcpy(p.lmk, lmk, 16);
    esp_now_add_peer(&p);
}

static void on_recv(const uint8_t *mac, const uint8_t *data, int len)
{
    if (len <= 0 || len > (int)sizeof(Rx::data)) return;
    Rx r;
    memcpy(r.mac, mac, 6);
    r.len = len;
    memcpy(r.data, data, len);
    xQueueSend(s_rxq, &r, 0);
}

static void save()
{
    Preferences p;
    p.begin("link", false);
    p.putBytes("mac", s_mac, 6);
    p.putBytes("lmk", s_lmk, 16);
    p.end();
}

static void enter_pairing()
{
    // A plain PAIR from a T-Deck we hold as an encrypted peer would be dropped
    if (s_paired) esp_now_del_peer(s_mac);
    esp_fill_random(s_priv, 32);
    if (!x25519(s_priv, nullptr, s_pub)) return;
    s_pairing = true;
    s_trial = false;
    Serial.println("[dongle] pairing: open Keyboard > Pair a dongle on the T-Deck");
}

static void leave_pairing()
{
    s_pairing = false;
    s_trial = false;
    memset(s_priv, 0, sizeof(s_priv));
    if (s_paired) add_peer(s_mac, s_lmk);
}

static void send_ctl(const uint8_t mac[6], const uint8_t lmk[16], uint8_t type)
{
    LinkCtl c;
    link_hdr(&c.h, type);
    link_tag(lmk, &c, offsetof(LinkCtl, tag), c.tag);
    esp_err_t e = esp_now_send(mac, (const uint8_t *)&c, sizeof(c));
    if (e != ESP_OK) Serial.printf("[dongle] esp_now_send %d: %s\n", type, esp_err_to_name(e));
}

static void handle_pair(const Rx &r)
{
    if (!s_pairing) return;
    const LinkKeyMsg *m = (const LinkKeyMsg *)r.data;
    uint8_t shared[32];
    if (!x25519(s_priv, m->pub, shared)) return;
    link_derive_lmk(shared, s_trial_lmk);
    memset(shared, 0, sizeof(shared));
    memcpy(s_trial_mac, r.mac, 6);
    add_peer(s_trial_mac, s_trial_lmk);
    s_trial = true;
    s_trial_at = millis();
    // The T-Deck switches to the new key once its PAIR is acknowledged; give it a moment
    s_paired_sends = 0;
    s_paired_sent_at = millis();
    Serial.println("[dongle] PAIR received, confirming");
}

// The T-Deck proved it holds the trial key: make it the pairing
static void commit_trial()
{
    if (s_paired && memcmp(s_mac, s_trial_mac, 6)) esp_now_del_peer(s_mac);
    memcpy(s_mac, s_trial_mac, 6);
    memcpy(s_lmk, s_trial_lmk, 16);
    s_paired = true;
    save();
    s_pairing = false;
    s_trial = false;
    memset(s_priv, 0, sizeof(s_priv));
    Serial.println("[dongle] paired");
}

static void handle(const Rx &r)
{
    if (link_hdr_ok(r.data, r.len, LINK_PAIR, sizeof(LinkKeyMsg))) { handle_pair(r); return; }

    // Everything else must come from the peer and carry its tag
    const uint8_t *lmk = nullptr;
    if (s_trial && !memcmp(r.mac, s_trial_mac, 6)) lmk = s_trial_lmk;
    else if (s_paired && !s_pairing && !memcmp(r.mac, s_mac, 6)) lmk = s_lmk;
    if (!lmk) return;

    const uint8_t *d = r.data;
    bool ok = false;
    if (link_hdr_ok(d, r.len, LINK_PING, sizeof(LinkCtl))) ok = link_tag_ok(lmk, d, offsetof(LinkCtl, tag));
    else if (link_hdr_ok(d, r.len, LINK_TAP, sizeof(LinkTap))) ok = link_tag_ok(lmk, d, offsetof(LinkTap, tag));
    else if (link_hdr_ok(d, r.len, LINK_MOUSE, sizeof(LinkMouse))) ok = link_tag_ok(lmk, d, offsetof(LinkMouse, tag));
    else if (link_hdr_ok(d, r.len, LINK_ACK, sizeof(LinkAck))) ok = link_tag_ok(lmk, d, offsetof(LinkAck, tag));
    else if (link_blob_ok(d, r.len)) ok = link_tag_ok(lmk, d, r.len - LINK_TAG_LEN);
    if (!ok) return;

    if (lmk == s_trial_lmk) {
        commit_trial();
        lmk = s_lmk;
    }
    s_last_rx = millis();

    switch (((const LinkHdr *)d)->type) {
    case LINK_PING:
        send_ctl(s_mac, s_lmk, LINK_PONG);
        break;
    case LINK_TAP: {
        const LinkTap *t = (const LinkTap *)d;
        KeyReport k = {};
        k.modifiers = t->mod;
        k.keys[0] = t->usage;
        Keyboard.sendReport(&k);
        memset(&k, 0, sizeof(k));
        Keyboard.sendReport(&k);
        note_tap(t->mod, t->usage);
        break;
    }
    case LINK_MOUSE: {
        const LinkMouse *m = (const LinkMouse *)d;
        uint8_t down = m->buttons & ~s_buttons, up = s_buttons & ~m->buttons;
        if (m->dx || m->dy || m->wheel) Mouse.move(m->dx, m->dy, m->wheel);
        if (down) Mouse.press(down);
        if (up) Mouse.release(up);
        s_buttons = m->buttons;
        break;
    }
    case LINK_ACK: {
        const LinkAck *a = (const LinkAck *)d;
        s_ack_ok = a->ok;
        s_ack_id = a->id;
        break;
    }
    case LINK_BLOB: {
        const LinkBlob *b = (const LinkBlob *)d;
        if (!s_reasm.feed(b)) break;
        send_ack(b->id, on_blob(s_reasm.kind, s_reasm.buf, s_reasm.len));
        s_reasm.mask = 0;
        break;
    }
    }
}

void setup()
{
    // Identity first, then every interface of the composite device, then USB.begin().
    // Our own product id: macOS caches the name by vendor:product, and Espressif's default
    // 0x303a:0x1001 is on file as a DevKit from the factory firmware (#55).
    USB.PID(0x81DE);
    USB.productName("T-Deck Keyboard Dongle");
    USB.manufacturerName("tdeck");
    Serial.begin(115200);
    Keyboard.begin();
    Mouse.begin();
#ifdef GEEK_SD
    sd_init();
#endif
    USB.begin();
    pinMode(BOOT_PIN, INPUT_PULLUP);
#if defined(LED_PIN)
    pinMode(LED_PIN, OUTPUT);
#endif
    led(false);
#ifdef GEEK_LCD
    screen_init();
#endif

    s_rxq = xQueueCreate(32, sizeof(Rx));
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.setSleep(false);
    esp_wifi_set_channel(LINK_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (esp_now_init() != ESP_OK) {
        Serial.println("[dongle] esp_now_init failed");
        return;
    }
    esp_now_set_pmk(LINK_PMK);
    esp_now_register_recv_cb(on_recv);
    add_peer(BROADCAST, nullptr);

    Preferences p;
    p.begin("link", true);
    s_paired = p.isKey("mac") && p.getBytes("mac", s_mac, 6) == 6 && p.getBytes("lmk", s_lmk, 16) == 16;
    p.end();
    if (s_paired) {
        add_peer(s_mac, s_lmk);
        Serial.printf("[dongle] paired with %02x:%02x:%02x:%02x:%02x:%02x\n", s_mac[0], s_mac[1], s_mac[2], s_mac[3], s_mac[4], s_mac[5]);
    } else {
        enter_pairing();
    }
}

void loop()
{
    Rx r;
    while (xQueueReceive(s_rxq, &r, 0) == pdTRUE) handle(r);
    uint32_t now = millis(); // after handle(): a PAIR just now must not look seconds old

    // BOOT held 2 s: pair again (the old pairing stays until a new one succeeds)
    static uint32_t boot_down = 0;
    if (digitalRead(BOOT_PIN) == LOW) {
        if (!boot_down) boot_down = now ? now : 1;
        else if (now - boot_down > 2000 && !s_pairing) enter_pairing();
    } else {
        boot_down = 0;
    }

    if (s_pairing && !s_trial && now - s_last_hello > 300) {
        s_last_hello = now;
        LinkKeyMsg m;
        link_hdr(&m.h, LINK_HELLO);
        memcpy(m.pub, s_pub, 32);
        esp_now_send(BROADCAST, (const uint8_t *)&m, sizeof(m));
    }
    if (s_trial) {
        // Keep confirming until the T-Deck's first encrypted packet proves the key took
        if (s_paired_sends < 16 && now - s_paired_sent_at > 250) {
            s_paired_sent_at = now;
            s_paired_sends++;
            send_ctl(s_trial_mac, s_trial_lmk, LINK_PAIRED);
        }
        // The T-Deck pings once a second after pairing; no word in 5 s means it didn't take
        if (now - s_trial_at > 5000) {
            Serial.println("[dongle] pairing not confirmed, listening again");
            esp_now_del_peer(s_trial_mac);
            s_trial = false;
        }
    }
    // Give up pairing mode after 2 minutes if we already have a T-Deck
    static uint32_t pairing_since = 0;
    if (s_pairing) {
        if (!pairing_since) pairing_since = now ? now : 1;
        else if (s_paired && now - pairing_since > 120000) leave_pairing();
    } else {
        pairing_since = 0;
    }

    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            s_line[s_line_len] = 0;
            if (s_line_len) host_line(s_line);
            s_line_len = 0;
        } else if (s_line_len + 1 < sizeof(s_line)) {
            s_line[s_line_len++] = c;
        }
    }
#ifdef GEEK_SD
    sd_tick(now);
    job_tick(now);
#endif

    bool online = s_paired && !s_pairing && s_last_rx && now - s_last_rx < 3000;
    static int last_state = -1;
    int state = s_pairing ? 0 : online ? 2 : 1;
    if (state != last_state) { last_state = state; json_link(); }
    if (s_pairing) led((now / 100) & 1);
    else if (online) led(true);
    else led((now / 1000) & 1);
#ifdef GEEK_LCD
    screen_draw(s_pairing ? SCR_PAIRING : online ? SCR_ONLINE : SCR_AWAY);
#endif
    delay(2);
}
