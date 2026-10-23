// USB keyboard + mouse that types what the T-Deck sends over ESP-NOW.
//
// Unpaired (or BOOT held for 2 s) it broadcasts HELLO until a T-Deck answers PAIR from
// the Keyboard app. The new pairing is only saved once the T-Deck's first encrypted
// packet checks out, so a pairing that half-failed leaves the old one in place.
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

// ESP-NOW callbacks run in the WiFi task; USB work happens in loop()
struct Rx { uint8_t mac[6]; uint8_t len; uint8_t data[48]; };
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
static uint32_t s_scr_taps = (uint32_t)-1;

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
    esp_now_send(mac, (const uint8_t *)&c, sizeof(c));
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
    }
}

void setup()
{
    Serial.begin(115200);
    USB.productName("T-Deck Keyboard Dongle");
    USB.manufacturerName("tdeck");
    Keyboard.begin();
    Mouse.begin();
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
    uint32_t now = millis();
    Rx r;
    while (xQueueReceive(s_rxq, &r, 0) == pdTRUE) handle(r);

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
        if (s_paired_sends < 3 && now - s_paired_sent_at > 100) {
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

    bool online = s_paired && !s_pairing && s_last_rx && now - s_last_rx < 3000;
    if (s_pairing) led((now / 100) & 1);
    else if (online) led(true);
    else led((now / 1000) & 1);
#ifdef GEEK_LCD
    screen_draw(s_pairing ? SCR_PAIRING : online ? SCR_ONLINE : SCR_AWAY);
#endif
    delay(2);
}
