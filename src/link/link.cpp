#include "link.h"
#include "link_proto.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include "mesh/crypto.h"

namespace dongle {

static bool s_running = false, s_paired = false, s_listen = false, s_ble = false;
static uint8_t s_mac[6], s_lmk[16];
static volatile uint32_t s_last_pong = 0;
static uint32_t s_last_ping = 0;
static volatile uint32_t s_old_peer_at = 0; // the paired dongle last spoke another link version

// The packet counter the dongle checks for replays. It is written to NVS in batches of
// CTR_BATCH, and a boot starts past the last batch, so it never repeats across resets.
static uint32_t s_ctr = 0, s_ctr_saved = 0;

// Lock-when-away setting (#168); loaded from NVS the first time it is read or sent
static uint16_t s_lock_after = 0;
static uint8_t s_lock_os = LINK_LOCK_OS_MAC;
static bool s_lock_pwr = false, s_lock_set = false, s_lock_loaded = false;
static const uint32_t CTR_BATCH = 256;

static void save_ctr()
{
    Preferences p;
    p.begin("link", false);
    p.putUInt("ctr", s_ctr_saved);
    p.end();
}

static uint32_t next_ctr()
{
    if (++s_ctr >= s_ctr_saved) { s_ctr_saved = s_ctr + CTR_BATCH; save_ctr(); }
    return s_ctr;
}

// HELLOs seen while listening, with their public keys
struct Candidate { uint8_t mac[6]; uint8_t pub[32]; uint32_t seen; };
static Candidate s_cand[4];
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static volatile PairState s_pair = PAIR_IDLE;
static uint8_t s_pending_mac[6], s_pending_lmk[16];
static uint32_t s_pair_at = 0;
static volatile bool s_pair_sent = false;
static bool s_pair_keyed = false, s_pair_applied = false;

// Blob packets are verified and reassembled in tick(), so the radio task only copies them
struct Rx { uint8_t len; uint8_t data[250]; };
static QueueHandle_t s_rxq = nullptr;
static LinkReasm s_reasm;
static blob_cb_t s_blob_cb = nullptr;
static uint8_t s_blob_id = 0;
static volatile int16_t s_ack_id = -1;   // last ACK seen: id, and whether it said ok
static volatile bool s_ack_ok = false;

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
    uint8_t v = link_version_of(data, len);
    if (v && v != LINK_VERSION) {
        if (s_paired && !memcmp(mac, s_mac, 6)) s_old_peer_at = now ? now : 1;
        return;
    }
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
        return;
    }
    if (!s_paired || memcmp(mac, s_mac, 6)) return;
    if (link_hdr_ok(data, len, LINK_ACK, sizeof(LinkAck))) {
        const LinkAck *a = (const LinkAck *)data;
        if (link_tag_ok(s_lmk, data, offsetof(LinkAck, tag))) { s_ack_ok = a->ok; s_ack_id = a->id; }
        return;
    }
    if (link_blob_ok(data, len) && s_rxq) {
        Rx r;
        r.len = len;
        memcpy(r.data, data, len);
        xQueueSend(s_rxq, &r, 0);
    }
}

static void load()
{
    Preferences p;
    p.begin("link", false); // read-write: creates the namespace on first run instead of logging an error
    s_paired = p.isKey("mac") && p.getBytes("mac", s_mac, 6) == 6 && p.getBytes("lmk", s_lmk, 16) == 16;
    s_ctr = p.getUInt("ctr", 0);
    p.end();
    s_ctr_saved = s_ctr + CTR_BATCH;
    save_ctr();
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
    // Power save would drop and delay packets, but the Bluetooth controller refuses to start
    // without it (coex_core_enable aborts), so sleep is on only while BLE is up (#151)
    WiFi.setSleep(s_ble);
    esp_wifi_set_channel(LINK_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (esp_now_init() != ESP_OK) return false;
    esp_now_set_pmk(LINK_PMK);
    esp_now_register_recv_cb(on_recv);
    esp_now_register_send_cb(on_sent);
    if (!s_rxq) s_rxq = xQueueCreate(8, sizeof(Rx));
    load();
    if (s_paired) add_peer(s_mac, s_lmk);
    s_last_pong = 0;
    s_running = true;
    return true;
}

// BYE and LOCK: a counter under the tag, sent a few times since nothing answers them
static bool send_ctl(uint8_t type, int times)
{
    if (!s_running || !s_paired) return false;
    bool sent = false;
    for (int i = 0; i < times; i++) {
        LinkCtl c;
        link_hdr(&c.h, type);
        c.ctr = next_ctr();
        link_tag(s_lmk, &c, offsetof(LinkCtl, tag), c.tag);
        sent |= esp_now_send(s_mac, (const uint8_t *)&c, sizeof(c)) == ESP_OK;
        delay(10);
    }
    return sent;
}

void bye()
{
    if (send_ctl(LINK_BYE, 3)) delay(20); // let the last one leave before the radio goes
}

static void load_lock()
{
    if (s_lock_loaded) return;
    s_lock_loaded = true;
    Preferences p;
    p.begin("link", false);
    s_lock_set = p.getBool("lk_set", false);
    s_lock_after = p.getUShort("lk_after", 0);
    s_lock_os = p.getUChar("lk_os", LINK_LOCK_OS_MAC);
    s_lock_pwr = p.getBool("lk_pwr", false);
    p.end();
}

// Three times 10 ms apart like BYE: nothing answers it
static bool send_lock_cfg()
{
    if (!s_running || !s_paired) return false;
    bool sent = false;
    for (int i = 0; i < 3; i++) {
        LinkLockCfg c;
        link_hdr(&c.h, LINK_LOCK_CFG);
        c.ctr = next_ctr();
        c.after = s_lock_after;
        c.os = s_lock_os;
        link_tag(s_lmk, &c, offsetof(LinkLockCfg, tag), c.tag);
        sent |= esp_now_send(s_mac, (const uint8_t *)&c, sizeof(c)) == ESP_OK;
        delay(10);
    }
    return sent;
}

void set_lock(uint16_t after_s, uint8_t os, bool on_power_off)
{
    load_lock();
    s_lock_after = after_s;
    s_lock_os = os;
    s_lock_pwr = on_power_off;
    s_lock_set = true;
    Preferences p;
    p.begin("link", false);
    p.putBool("lk_set", true);
    p.putUShort("lk_after", s_lock_after);
    p.putUChar("lk_os", s_lock_os);
    p.putBool("lk_pwr", s_lock_pwr);
    p.end();
    send_lock_cfg();
}

uint16_t lock_after() { load_lock(); return s_lock_after; }
uint8_t lock_os() { load_lock(); return s_lock_os; }
bool lock_on_power_off() { load_lock(); return s_lock_pwr; }

// Powering off on purpose: LOCK once and no BYE when the setting asks for it, so if that
// one LOCK is lost the dongle's away timer still locks; otherwise BYE so it does not
void power_off()
{
    load_lock();
    if (s_lock_pwr) { if (send_ctl(LINK_LOCK, 1)) delay(20); }
    else bye();
}

bool lock_computer() { return send_ctl(LINK_LOCK, 1); }

void stop()
{
    if (!s_running) return;
    bye();
    s_running = false;
    s_listen = false;
    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();
    WiFi.mode(WIFI_OFF);
}

bool running() { return s_running; }
void ble_sharing(bool on)
{
    s_ble = on;
    if (s_running) WiFi.setSleep(on);
}
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
    save_ctr(); // the counter outlives the pairing: a new dongle must never see an old value
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
    // Offline -> online: the dongle may have been reflashed while away, so give it the
    // lock setting again, once it has ever been set on this T-Deck (#168)
    static bool was_online = false;
    bool now_online = online();
    if (now_online && !was_online) { load_lock(); if (s_lock_set) send_lock_cfg(); }
    was_online = now_online;
    // The WiFi and Spectrum apps turn WiFi off when they close, and a scan leaves the radio
    // on another channel; either silences the link. A quiet link brings the radio back and
    // returns to its channel every 2 s until the dongle answers (#132).
    static uint32_t chan_at = 0;
    if (s_paired && !online() && millis() - chan_at > 2000) {
        chan_at = millis();
        bool prom = false;
        esp_wifi_get_promiscuous(&prom);
        if (!prom && WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
            if (!(WiFi.getMode() & WIFI_MODE_STA)) {
                Serial.println("[link] WiFi was off: starting the link again");
                esp_now_unregister_recv_cb();
                esp_now_unregister_send_cb();
                esp_now_deinit();
                s_running = false;
                start();
                return;
            }
            esp_wifi_set_channel(LINK_CHANNEL, WIFI_SECOND_CHAN_NONE);
        }
        // A dongle that rebooted holds a replay floor up to a batch ahead of our counter and
        // drops us until we pass it; a quiet link jumps a batch ahead, at most once a minute
        static uint32_t skip_at = 0;
        if (millis() - skip_at > 60000) {
            skip_at = millis();
            s_ctr += CTR_BATCH;
            if (s_ctr >= s_ctr_saved) { s_ctr_saved = s_ctr + CTR_BATCH; save_ctr(); }
        }
    }
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
    // Complete blobs from the dongle, each answered with an ACK
    Rx r;
    while (s_rxq && xQueueReceive(s_rxq, &r, 0) == pdTRUE) {
        if (!link_tag_ok(s_lmk, r.data, r.len - LINK_TAG_LEN)) continue;
        const LinkBlob *b = (const LinkBlob *)r.data;
        if (!s_reasm.feed(b)) continue;
        LinkAck a;
        link_hdr(&a.h, LINK_ACK);
        a.id = b->id;
        a.ok = 1;
        link_tag(s_lmk, &a, offsetof(LinkAck, tag), a.tag);
        esp_now_send(s_mac, (const uint8_t *)&a, sizeof(a));
        if (s_blob_cb) s_blob_cb(s_reasm.kind, s_reasm.buf, s_reasm.len);
        s_reasm.mask = 0;
    }
    if (s_paired && millis() - s_last_ping > 1000) {
        s_last_ping = millis();
        LinkCtl c;
        link_hdr(&c.h, LINK_PING);
        c.ctr = next_ctr();
        link_tag(s_lmk, &c, offsetof(LinkCtl, tag), c.tag);
        esp_err_t e = esp_now_send(s_mac, (const uint8_t *)&c, sizeof(c));
        static uint32_t said = 0;
        if (e != ESP_OK && millis() - said > 5000) { said = millis(); Serial.printf("[link] ping: %s\n", esp_err_to_name(e)); }
    }
}

bool tap(uint8_t mod, uint8_t usage)
{
    if (!s_running || !s_paired) return false;
    LinkTap t;
    link_hdr(&t.h, LINK_TAP);
    t.mod = mod;
    t.usage = usage;
    t.ctr = next_ctr();
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
    m.ctr = next_ctr();
    link_tag(s_lmk, &m, offsetof(LinkMouse, tag), m.tag);
    return esp_now_send(s_mac, (const uint8_t *)&m, sizeof(m)) == ESP_OK;
}

void set_blob_cb(blob_cb_t cb) { s_blob_cb = cb; }

bool old_firmware() { return s_old_peer_at && millis() - s_old_peer_at < 10000; }

bool send_blob(uint8_t kind, const void *data, size_t len, uint8_t *id_out)
{
    if (!s_running || !s_paired || len == 0 || len > LINK_BLOB_MAX * LINK_BLOB_PARTS) return false;
    uint8_t id = ++s_blob_id;
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
    if (id_out) *id_out = id;
    return true;
}

bool take_ack(uint8_t id, bool *ok)
{
    if (s_ack_id != id) return false;
    *ok = s_ack_ok;
    s_ack_id = -1;
    return true;
}

} // namespace dongle
