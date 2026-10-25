#include "link.h"
#include "link_proto.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include "mesh/crypto.h"

namespace dongle {

static bool s_running = false, s_paired = false, s_listen = false;
static uint8_t s_mac[6], s_lmk[16];
static volatile uint32_t s_last_pong = 0;
static uint32_t s_last_ping = 0;

// HELLOs seen while listening, with their public keys
struct Candidate { uint8_t mac[6]; uint8_t pub[32]; uint32_t seen; };
static Candidate s_cand[4];
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static volatile PairState s_pair = PAIR_IDLE;
static uint8_t s_pending_mac[6], s_pending_lmk[16];
static uint32_t s_pair_at = 0;
static volatile bool s_pair_sent = false;
static bool s_pair_keyed = false, s_pair_applied = false;

// PAIR must leave unencrypted, so the peer only switches to the new key once it's sent
static void on_sent(const uint8_t *mac, esp_now_send_status_t)
{
    if (s_pair == PAIR_WAITING && !memcmp(mac, s_pending_mac, 6)) s_pair_sent = true;
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
    uint32_t now = millis();
    if (link_hdr_ok(data, len, LINK_HELLO, sizeof(LinkKeyMsg))) {
        if (!s_listen) return;
        const LinkKeyMsg *m = (const LinkKeyMsg *)data;
        portENTER_CRITICAL(&s_mux);
        int slot = -1, oldest = 0;
        for (int i = 0; i < 4; i++) {
            if (!memcmp(s_cand[i].mac, mac, 6) && s_cand[i].seen) { slot = i; break; }
            if (s_cand[i].seen < s_cand[oldest].seen) oldest = i;
        }
        if (slot < 0) slot = oldest;
        memcpy(s_cand[slot].mac, mac, 6);
        memcpy(s_cand[slot].pub, m->pub, 32);
        s_cand[slot].seen = now ? now : 1;
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    if (link_hdr_ok(data, len, LINK_PAIRED, sizeof(LinkCtl))) {
        if (s_pair == PAIR_WAITING && !memcmp(mac, s_pending_mac, 6) &&
            link_tag_ok(s_pending_lmk, data, offsetof(LinkCtl, tag)))
            s_pair = PAIR_DONE;
        return;
    }
    if (link_hdr_ok(data, len, LINK_PONG, sizeof(LinkCtl))) {
        if (s_paired && !memcmp(mac, s_mac, 6) && link_tag_ok(s_lmk, data, offsetof(LinkCtl, tag))) s_last_pong = now;
    }
}

static void load()
{
    Preferences p;
    p.begin("link", false); // read-write: creates the namespace on first run instead of logging an error
    s_paired = p.isKey("mac") && p.getBytes("mac", s_mac, 6) == 6 && p.getBytes("lmk", s_lmk, 16) == 16;
    p.end();
}

static void save()
{
    Preferences p;
    p.begin("link", false);
    p.putBytes("mac", s_mac, 6);
    p.putBytes("lmk", s_lmk, 16);
    p.end();
}

bool start()
{
    if (s_running) return true;
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.setSleep(false); // power save would drop and delay packets
    esp_wifi_set_channel(LINK_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (esp_now_init() != ESP_OK) return false;
    esp_now_set_pmk(LINK_PMK);
    esp_now_register_recv_cb(on_recv);
    esp_now_register_send_cb(on_sent);
    load();
    if (s_paired) add_peer(s_mac, s_lmk);
    s_last_pong = 0;
    s_running = true;
    return true;
}

void stop()
{
    if (!s_running) return;
    s_running = false;
    s_listen = false;
    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();
    WiFi.mode(WIFI_OFF);
}

bool running() { return s_running; }
bool paired() { return s_paired; }
void peer_mac(uint8_t mac[6]) { memcpy(mac, s_mac, 6); }
bool online() { return s_paired && s_last_pong && millis() - s_last_pong < 3000; }

void forget()
{
    if (s_paired && s_running) esp_now_del_peer(s_mac);
    s_paired = false;
    Preferences p;
    p.begin("link", false);
    p.clear();
    p.end();
}

void pair_listen(bool on)
{
    s_listen = on;
    portENTER_CRITICAL(&s_mux);
    memset(s_cand, 0, sizeof(s_cand));
    portEXIT_CRITICAL(&s_mux);
    s_pair = PAIR_IDLE;
}

size_t candidates(uint8_t macs[][6], size_t max)
{
    size_t n = 0;
    uint32_t now = millis();
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < 4 && n < max; i++)
        if (s_cand[i].seen && now - s_cand[i].seen < 5000) memcpy(macs[n++], s_cand[i].mac, 6);
    portEXIT_CRITICAL(&s_mux);
    return n;
}

bool pair_with(const uint8_t mac[6])
{
    uint8_t dongle_pub[32];
    bool found = false;
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < 4; i++)
        if (s_cand[i].seen && !memcmp(s_cand[i].mac, mac, 6)) { memcpy(dongle_pub, s_cand[i].pub, 32); found = true; }
    portEXIT_CRITICAL(&s_mux);
    if (!found || !s_running) return false;

    // Fresh keypair per pairing; the private half never leaves this function
    uint8_t priv[32], pub[32], shared[32];
    if (!crypto::x25519_generate(priv, pub) || !crypto::x25519_shared(priv, dongle_pub, shared)) return false;
    memset(priv, 0, sizeof(priv));
    link_derive_lmk(shared, s_pending_lmk);
    memset(shared, 0, sizeof(shared));
    memcpy(s_pending_mac, mac, 6);

    // PAIR goes out plain; the answer only decrypts once the dongle holds the same key
    add_peer(mac, nullptr);
    LinkKeyMsg m;
    link_hdr(&m.h, LINK_PAIR);
    memcpy(m.pub, pub, 32);
    s_pair_sent = s_pair_keyed = s_pair_applied = false;
    s_pair = PAIR_WAITING;
    s_pair_at = millis();
    if (esp_now_send(mac, (const uint8_t *)&m, sizeof(m)) != ESP_OK) { s_pair = PAIR_FAILED; return false; }
    return true;
}

PairState pair_state() { return s_pair; }

void tick()
{
    if (!s_running) return;
    if (s_pair == PAIR_WAITING && s_pair_sent && !s_pair_keyed) {
        add_peer(s_pending_mac, s_pending_lmk); // the dongle answers encrypted
        s_pair_keyed = true;
    }
    if (s_pair == PAIR_WAITING && millis() - s_pair_at > 3000) {
        s_pair = PAIR_FAILED;
        if (s_paired) add_peer(s_mac, s_lmk); else esp_now_del_peer(s_pending_mac);
    }
    if (s_pair == PAIR_DONE && !s_pair_applied) {
        s_pair_applied = true;
        if (s_paired && memcmp(s_mac, s_pending_mac, 6)) esp_now_del_peer(s_mac);
        memcpy(s_mac, s_pending_mac, 6);
        memcpy(s_lmk, s_pending_lmk, 16);
        s_paired = true;
        save();
        s_last_pong = millis();
        s_listen = false;
        Serial.println("[link] paired");
    }
    if (s_paired && millis() - s_last_ping > 1000) {
        s_last_ping = millis();
        LinkCtl c;
        link_hdr(&c.h, LINK_PING);
        link_tag(s_lmk, &c, offsetof(LinkCtl, tag), c.tag);
        esp_now_send(s_mac, (const uint8_t *)&c, sizeof(c));
    }
}

bool tap(uint8_t mod, uint8_t usage)
{
    if (!s_running || !s_paired) return false;
    LinkTap t;
    link_hdr(&t.h, LINK_TAP);
    t.mod = mod;
    t.usage = usage;
    link_tag(s_lmk, &t, offsetof(LinkTap, tag), t.tag);
    return esp_now_send(s_mac, (const uint8_t *)&t, sizeof(t)) == ESP_OK;
}

bool mouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel)
{
    if (!s_running || !s_paired) return false;
    LinkMouse m;
    link_hdr(&m.h, LINK_MOUSE);
    m.buttons = buttons;
    m.dx = dx;
    m.dy = dy;
    m.wheel = wheel;
    link_tag(s_lmk, &m, offsetof(LinkMouse, tag), m.tag);
    return esp_now_send(s_mac, (const uint8_t *)&m, sizeof(m)) == ESP_OK;
}

} // namespace dongle
