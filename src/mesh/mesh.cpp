#include "mesh.h"
#include <Arduino.h>
#include <RadioLib.h>
#include <sys/time.h>
#include "board.h"
#include "protocol.h"
#include "pb.h"
#include "crypto.h"
#include "core/settings.h"
#include "core/nodedb.h"
#include "hal/power.h"

namespace mesh {

using namespace mt;

static SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);
static bool s_radio_ok = false;
static volatile bool s_irq = false;
static bool s_transmitting = false;
static uint32_t s_tx_started = 0;
static uint32_t s_our_num = 0;
static uint8_t s_key[32];
static size_t s_key_len = 0;
static uint8_t s_hash = 0;
static float s_slot_ms = 44;
static Stats s_stats = {};
static text_cb_t s_text_cb = nullptr;
static uint32_t s_rolling_id = 0;

static int32_t s_lat = 0, s_lon = 0, s_alt = 0;
static uint8_t s_sats = 0;
static bool s_has_pos = false;

// ---- outbound queue ------------------------------------------------------

struct TxItem {
    bool used;
    uint8_t len;
    uint8_t frame[MAX_LORA_LEN];
    uint32_t due_ms;
    uint32_t from, id;
    bool relay;           // cancel if someone else relays first
};
static TxItem s_txq[12];

// Reliable delivery of our own want_ack unicasts
struct Pending {
    bool used;
    uint32_t id, to;
    uint8_t len, tries;
    uint8_t frame[MAX_LORA_LEN];
    uint32_t next_ms;
};
static Pending s_pending[6];

// Recently seen (from,id) pairs
static struct { uint32_t from, id; } s_seen[128];
static uint8_t s_seen_pos = 0;

static void IRAM_ATTR on_dio1() { s_irq = true; }

static bool seen(uint32_t from, uint32_t id)
{
    for (auto &s : s_seen)
        if (s.id == id && s.from == from) return true;
    return false;
}

static void remember(uint32_t from, uint32_t id)
{
    s_seen[s_seen_pos] = {from, id};
    s_seen_pos = (s_seen_pos + 1) % 128;
}

static uint32_t now_epoch()
{
    time_t t = time(nullptr);
    return t > 1700000000 ? (uint32_t)t : 0;
}

static uint32_t last_heard_stamp()
{
    uint32_t t = now_epoch();
    return t ? t : millis() / 1000;
}

static uint32_t new_id()
{
    if (!s_rolling_id) s_rolling_id = crypto::random32();
    s_rolling_id = (s_rolling_id + 1) & 0x3FF;
    uint32_t id = s_rolling_id | (crypto::random32() << 10);
    return id ? id : 1;
}

static bool enqueue(const uint8_t *frame, size_t len, uint32_t delay_ms, bool relay)
{
    for (auto &t : s_txq) {
        if (t.used) continue;
        t.used = true;
        t.len = len;
        memcpy(t.frame, frame, len);
        t.due_ms = millis() + delay_ms;
        Header h = header_read(frame);
        t.from = h.from;
        t.id = h.id;
        t.relay = relay;
        return true;
    }
    return false;
}

static void cancel_relay(uint32_t from, uint32_t id)
{
    for (auto &t : s_txq)
        if (t.used && t.relay && t.from == from && t.id == id) t.used = false;
}

// ---- radio --------------------------------------------------------------

static void start_rx()
{
    s_transmitting = false;
    radio.startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF,
                       RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED),
                       RADIOLIB_IRQ_RX_DEFAULT_MASK);
}

static bool configure_radio()
{
    const Preset &p = PRESETS[settings.preset < NUM_PRESETS ? settings.preset : 0];
    const char *chan = settings.channel_name[0] ? settings.channel_name : p.name;
    s_stats.freq_mhz = slot_freq_mhz(chan, p.bw_khz);
    s_slot_ms = slot_time_ms(p.bw_khz, p.sf);
    s_key_len = expand_psk(settings.psk, settings.psk_len, s_key);
    s_hash = channel_hash(chan, s_key, s_key_len);
    s_stats.channel_hash = s_hash;

    int8_t power = settings.tx_power > 22 ? 22 : settings.tx_power;
    // TCXO on DIO3 at 1.8 V and DIO2 as RF switch, as Meshtastic's t-deck variant
    int st = radio.begin(s_stats.freq_mhz, p.bw_khz, p.sf, p.cr, LORA_SYNC_WORD, power, LORA_PREAMBLE, 1.8f, false);
    if (st != RADIOLIB_ERR_NONE) {
        Serial.printf("radio.begin failed %d\n", st);
        return false;
    }
    radio.setDio2AsRfSwitch(true);
    radio.setCurrentLimit(140);
    radio.setCRC(2);
    radio.setRxBoostedGainMode(true);
    radio.setDio1Action(on_dio1);
    start_rx();
    Serial.printf("LoRa %s %.3f MHz hash 0x%02x\n", chan, s_stats.freq_mhz, s_hash);
    return true;
}

static bool actively_receiving()
{
    static uint32_t busy_since = 0;
    uint32_t f = radio.getIrqFlags();
    bool busy = f & (RADIOLIB_SX126X_IRQ_HEADER_VALID | RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED);
    if (!busy) { busy_since = 0; return false; }
    if (!busy_since) busy_since = millis();
    if (millis() - busy_since > 4000) {
        // A false preamble detect never completes; restart RX to clear it
        busy_since = 0;
        start_rx();
        return false;
    }
    return true;
}

// ---- encoding -----------------------------------------------------------

struct DataMsg {
    uint32_t portnum = 0;
    const uint8_t *payload = nullptr;
    size_t payload_len = 0;
    bool want_response = false;
    uint32_t request_id = 0, reply_id = 0, emoji = 0;
};

static size_t encode_data(const DataMsg &d, uint8_t *out, size_t cap)
{
    PbWriter w(out, cap);
    w.varint(1, d.portnum);
    if (d.payload_len) w.bytes(2, d.payload, d.payload_len);
    if (d.want_response) w.boolean(3, true);
    if (d.request_id) w.fixed32(6, d.request_id);
    if (d.reply_id) w.fixed32(7, d.reply_id);
    if (d.emoji) w.fixed32(8, d.emoji);
    w.varint(9, d.want_response ? 2 : 0); // bitfield: ok_to_mqtt off, want_response bit 1
    return w.ok() ? w.len() : 0;
}

static bool decode_data(const uint8_t *buf, size_t len, DataMsg &d)
{
    PbReader r(buf, len);
    while (r.next()) {
        switch (r.field) {
        case 1: d.portnum = (uint32_t)r.varint(); break;
        case 2: d.payload = r.bytes(&d.payload_len); break;
        case 3: d.want_response = r.varint() != 0; break;
        case 6: d.request_id = r.fixed32(); break;
        case 7: d.reply_id = r.fixed32(); break;
        case 8: d.emoji = r.fixed32(); break;
        case 9: if (r.varint() & 2) d.want_response = true; break;
        default: r.skip();
        }
    }
    return r.ok() && d.portnum != PORT_UNKNOWN;
}

// Build, encrypt and queue a packet from us. Returns the packet id (0 on failure).
static uint32_t send_data(uint32_t to, const DataMsg &d, bool want_ack, uint8_t hop_limit = 0xFF)
{
    if (!s_radio_ok) return 0;
    uint8_t frame[MAX_LORA_LEN];
    uint8_t plain[MAX_LORA_LEN];
    size_t n = encode_data(d, plain, sizeof(plain));
    if (!n) return 0;

    Header h = {};
    h.to = to;
    h.from = s_our_num;
    h.id = new_id();
    uint8_t hops = hop_limit == 0xFF ? settings.hop_limit : hop_limit;
    h.set_hops(hops, hops);
    if (want_ack) h.flags |= FLAG_WANT_ACK;
    h.relay_node = s_our_num & 0xFF;

    Node *peer = to != BROADCAST ? nodedb::find(to) : nullptr;
    bool pki = peer && peer->has_key && settings.has_keys &&
               d.portnum != PORT_NODEINFO && d.portnum != PORT_ROUTING &&
               d.portnum != PORT_POSITION && d.portnum != PORT_TRACEROUTE;
    size_t total;
    if (pki) {
        if (HEADER_LEN + n + PKC_OVERHEAD > MAX_LORA_LEN) return 0;
        h.channel = 0;
        if (!crypto::pki_encrypt(settings.priv_key, peer->pub_key, h.from, h.id, plain, n, frame + HEADER_LEN))
            return 0;
        total = HEADER_LEN + n + PKC_OVERHEAD;
    } else {
        if (HEADER_LEN + n > MAX_LORA_LEN) return 0;
        h.channel = s_hash;
        memcpy(frame + HEADER_LEN, plain, n);
        crypto::aes_ctr(s_key, s_key_len, h.from, h.id, frame + HEADER_LEN, n);
        total = HEADER_LEN + n;
    }
    header_write(h, frame);
    remember(h.from, h.id);

    uint32_t delay = (uint32_t)(random(0, 1 << 3) * s_slot_ms);
    if (!enqueue(frame, total, delay, false)) return 0;

    if (want_ack && to != BROADCAST) {
        for (auto &p : s_pending) {
            if (p.used) continue;
            p.used = true;
            p.id = h.id;
            p.to = to;
            p.len = total;
            memcpy(p.frame, frame, total);
            p.tries = 0;
            p.next_ms = millis() + 20000;
            break;
        }
    }
    return h.id;
}

static void encode_user(PbWriter &w)
{
    char id[12];
    snprintf(id, sizeof(id), "!%08x", (unsigned)s_our_num);
    w.string(1, id);
    w.string(2, settings.long_name);
    w.string(3, settings.short_name);
    w.varint(5, HW_MODEL_T_DECK);
    w.varint(7, settings.rebroadcast ? 0 : 1); // CLIENT or CLIENT_MUTE
    if (settings.has_keys) w.bytes(8, settings.pub_key, 32);
}

void send_nodeinfo(uint32_t to, bool want_response)
{
    uint8_t buf[160];
    PbWriter w(buf, sizeof(buf));
    encode_user(w);
    DataMsg d;
    d.portnum = PORT_NODEINFO;
    d.payload = buf;
    d.payload_len = w.len();
    d.want_response = want_response;
    send_data(to, d, false);
}

static int32_t reduce_precision(int32_t v, uint8_t bits)
{
    if (bits >= 32 || bits == 0) return v;
    uint32_t mask = UINT32_MAX << (32 - bits);
    return (int32_t)(((uint32_t)v & mask) + (1u << (31 - bits)));
}

void send_position(uint32_t to)
{
    if (!s_has_pos) return;
    uint8_t buf[64];
    PbWriter w(buf, sizeof(buf));
    uint8_t prec = settings.position_precision;
    w.sfixed32(1, reduce_precision(s_lat, prec));
    w.sfixed32(2, reduce_precision(s_lon, prec));
    w.int32(3, s_alt);
    if (uint32_t t = now_epoch()) w.fixed32(4, t);
    w.varint(5, 2); // LOC_INTERNAL (onboard GPS)
    w.varint(19, s_sats);
    w.varint(23, prec);
    DataMsg d;
    d.portnum = PORT_POSITION;
    d.payload = buf;
    d.payload_len = w.len();
    send_data(to, d, false);
}

void send_telemetry()
{
    uint8_t dm[40], buf[64];
    PbWriter m(dm, sizeof(dm));
    m.varint(1, power_usb_powered() ? 101 : power_battery_percent());
    m.float32(2, power_battery_mv() / 1000.0f);
    m.varint(5, millis() / 1000);
    PbWriter w(buf, sizeof(buf));
    if (uint32_t t = now_epoch()) w.fixed32(1, t);
    w.bytes(2, dm, m.len());
    DataMsg d;
    d.portnum = PORT_TELEMETRY;
    d.payload = buf;
    d.payload_len = w.len();
    send_data(BROADCAST, d, false);
}

uint32_t send_text(uint32_t to, const char *text)
{
    size_t n = strlen(text);
    if (n == 0 || n > 200) return 0;
    DataMsg d;
    d.portnum = PORT_TEXT;
    d.payload = (const uint8_t *)text;
    d.payload_len = n;
    uint32_t id = send_data(to, d, true);
    Message m = {};
    m.from = s_our_num;
    m.to = to;
    m.id = id;
    m.time = now_epoch();
    m.status = id ? MSG_QUEUED : MSG_FAILED;
    Node *peer = to != BROADCAST ? nodedb::find(to) : nullptr;
    m.pki = peer && peer->has_key;
    strlcpy(m.text, text, sizeof(m.text));
    messages::add(m);
    if (to != BROADCAST && !(peer && peer->has_key)) {
        // 2.7+ nodes drop channel-encrypted DMs: ask for their key
        send_nodeinfo(to, true);
    }
    return id;
}

static void send_ack(const Header &orig, uint8_t error = 0)
{
    uint8_t buf[4];
    PbWriter w(buf, sizeof(buf));
    w.varint(3, error); // Routing.error_reason (oneof: always present)
    DataMsg d;
    d.portnum = PORT_ROUTING;
    d.payload = buf;
    d.payload_len = w.len();
    d.request_id = orig.id;
    uint8_t hops = settings.hop_limit;
    if (orig.hop_start() == 0) hops = 0;
    else if ((uint8_t)(orig.hop_start() - orig.hop_limit() + 2) < hops) hops = orig.hop_start() - orig.hop_limit() + 2;
    send_data(orig.from, d, false, hops);
}

// ---- inbound ------------------------------------------------------------

static void handle_user(Node *n, const uint8_t *p, size_t len)
{
    PbReader r(p, len);
    while (r.next()) {
        switch (r.field) {
        case 2: r.string(n->long_name, sizeof(n->long_name)); break;
        case 3: r.string(n->short_name, sizeof(n->short_name)); break;
        case 5: n->hw_model = (uint8_t)r.varint(); break;
        case 7: n->role = (uint8_t)r.varint(); break;
        case 8: {
            size_t kl;
            const uint8_t *k = r.bytes(&kl);
            if (k && kl == 32) { memcpy(n->pub_key, k, 32); n->has_key = true; }
            break;
        }
        default: r.skip();
        }
    }
    if (r.ok()) n->has_user = true;
}

static void handle_position(Node *n, const uint8_t *p, size_t len)
{
    PbReader r(p, len);
    int32_t lat = 0, lon = 0;
    bool has_lat = false, has_lon = false;
    while (r.next()) {
        switch (r.field) {
        case 1: lat = (int32_t)r.fixed32(); has_lat = true; break;
        case 2: lon = (int32_t)r.fixed32(); has_lon = true; break;
        case 3: n->alt = (int32_t)r.varint(); break;
        case 4: n->pos_time = r.fixed32(); break;
        default: r.skip();
        }
    }
    if (r.ok() && has_lat && has_lon && (lat || lon)) {
        n->lat_i = lat;
        n->lon_i = lon;
        n->has_pos = true;
    }
    // No GPS fix of our own yet: take the clock from a recent neighbour report
    if (n->pos_time > 1700000000 && !now_epoch()) {
        struct timeval tv = {(time_t)n->pos_time, 0};
        settimeofday(&tv, nullptr);
    }
}

static void handle_telemetry(Node *n, const uint8_t *p, size_t len)
{
    PbReader r(p, len);
    while (r.next()) {
        if (r.field == 2) {
            size_t ml;
            const uint8_t *m = r.bytes(&ml);
            PbReader dm(m, ml);
            while (dm.next()) {
                if (dm.field == 1) n->battery = (uint8_t)dm.varint();
                else if (dm.field == 2) n->voltage = dm.float32();
                else dm.skip();
            }
        } else {
            r.skip();
        }
    }
}

static void deliver(const Header &h, const DataMsg &d, bool pki, Node *n)
{
    switch (d.portnum) {
    case PORT_TEXT: {
        Message m = {};
        m.from = h.from;
        m.to = h.to;
        m.id = h.id;
        m.time = now_epoch();
        m.status = MSG_RECEIVED;
        m.pki = pki;
        m.unread = true;
        size_t n2 = d.payload_len < sizeof(m.text) - 1 ? d.payload_len : sizeof(m.text) - 1;
        memcpy(m.text, d.payload, n2);
        m.text[n2] = 0;
        Message *stored = messages::add(m);
        if (s_text_cb) s_text_cb(*stored);
        break;
    }
    case PORT_NODEINFO: {
        handle_user(n, d.payload, d.payload_len);
        static uint32_t last_reply = 0;
        if (d.want_response && (h.to == s_our_num || h.to == BROADCAST) &&
            (millis() - last_reply > 60000 || h.to == s_our_num)) {
            last_reply = millis();
            send_nodeinfo(h.from, false);
        }
        break;
    }
    case PORT_POSITION:
        handle_position(n, d.payload, d.payload_len);
        if (d.want_response && h.to == s_our_num && settings.share_position) send_position(h.from);
        break;
    case PORT_TELEMETRY:
        handle_telemetry(n, d.payload, d.payload_len);
        break;
    case PORT_ROUTING:
        if (d.request_id && h.to == s_our_num) {
            PbReader r(d.payload, d.payload_len);
            uint32_t err = 0;
            while (r.next()) { if (r.field == 3) err = (uint32_t)r.varint(); else r.skip(); }
            for (auto &p : s_pending)
                if (p.used && p.id == d.request_id) p.used = false;
            messages::set_status(d.request_id, err == 0 ? MSG_ACKED : MSG_FAILED);
        }
        break;
    default:
        break;
    }
    nodedb::mark_dirty();
}

static void handle_frame(uint8_t *frame, size_t len, float rssi, float snr)
{
    if (len < HEADER_LEN) { s_stats.rx_bad++; return; }
    Header h = header_read(frame);
    s_stats.last_rssi = rssi;
    s_stats.last_snr = snr;

    if (h.from == s_our_num) {
        // Someone relayed our packet: implicit ACK for broadcasts
        messages::set_status(h.id, MSG_RELAYED);
        return;
    }
    if (h.id == 0) return;
    if (seen(h.from, h.id)) {
        s_stats.rx_dupe++;
        cancel_relay(h.from, h.id);
        return;
    }
    remember(h.from, h.id);
    s_stats.rx_ok++;

    Node *n = nodedb::get_or_create(h.from);
    n->last_heard = last_heard_stamp();
    bool direct = h.hop_start() == 0 || h.hop_start() == h.hop_limit();
    if (direct) {
        // SNR/RSSI describe the last hop, so only record them for direct packets
        n->snr = snr;
        n->rssi = (int16_t)rssi;
    }
    if (h.hop_start() && h.hop_start() >= h.hop_limit()) n->hops_away = h.hop_start() - h.hop_limit();
    nodedb::mark_dirty();

    uint8_t *enc = frame + HEADER_LEN;
    size_t enc_len = len - HEADER_LEN;
    uint8_t plain[MAX_LORA_LEN];
    DataMsg d;
    bool decoded = false, pki = false;

    if (h.channel == 0 && h.to == s_our_num && enc_len > PKC_OVERHEAD && n->has_key) {
        if (crypto::pki_decrypt(settings.priv_key, n->pub_key, h.from, h.id, enc, enc_len, plain)) {
            decoded = decode_data(plain, enc_len - PKC_OVERHEAD, d);
            pki = decoded;
        }
    } else if (h.channel == s_hash) {
        memcpy(plain, enc, enc_len);
        crypto::aes_ctr(s_key, s_key_len, h.from, h.id, plain, enc_len);
        decoded = decode_data(plain, enc_len, d);
    }

    if (decoded) {
        deliver(h, d, pki, n);
        if (h.to == s_our_num && h.want_ack() && d.portnum != PORT_ROUTING) send_ack(h);
    } else if (h.channel == 0 && h.to == s_our_num && !n->has_key) {
        // PKI DM from a node whose key we lack: ask for it
        send_nodeinfo(h.from, true);
    }

    // Managed flooding relay (CLIENT role)
    bool for_us = h.to == s_our_num;
    bool next_hop_ok = h.next_hop == 0 || h.next_hop == (s_our_num & 0xFF);
    if (settings.rebroadcast && !for_us && h.hop_limit() > 0 && next_hop_ok) {
        Header rh = h;
        rh.set_hops(h.hop_limit() - 1, h.hop_start());
        rh.relay_node = s_our_num & 0xFF;
        header_write(rh, frame);
        // Lower SNR (farther) nodes relay sooner; clients wait behind routers
        float s = snr < -20 ? -20 : snr > 10 ? 10 : snr;
        int cw = 3 + (int)((s + 20) * 5 / 30);
        uint32_t delay = (uint32_t)(2 * 8 * s_slot_ms + random(0, 1 << cw) * s_slot_ms);
        if (enqueue(frame, len, delay, true)) s_stats.relayed++;
    }
}

// ---- main loop ----------------------------------------------------------

static void service_radio()
{
    if (s_irq) {
        s_irq = false;
        if (s_transmitting) {
            radio.finishTransmit();
            s_stats.tx++;
            start_rx();
        } else {
            uint8_t buf[MAX_LORA_LEN];
            size_t len = radio.getPacketLength();
            int st = radio.readData(buf, len > sizeof(buf) ? sizeof(buf) : len);
            float rssi = radio.getRSSI(), snr = radio.getSNR();
            start_rx();
            if (st == RADIOLIB_ERR_NONE) handle_frame(buf, len, rssi, snr);
            else s_stats.rx_bad++;
        }
    }

    // TX watchdog: SF12 frames can take ~3 s
    if (s_transmitting && millis() - s_tx_started > 8000) start_rx();
    if (s_transmitting) return;

    // Pick the earliest due item
    TxItem *next = nullptr;
    for (auto &t : s_txq)
        if (t.used && (int32_t)(millis() - t.due_ms) >= 0 && (!next || (int32_t)(t.due_ms - next->due_ms) < 0)) next = &t;
    if (!next) return;

    if (actively_receiving()) {
        next->due_ms = millis() + (uint32_t)(random(1, 8) * s_slot_ms);
        return;
    }
    s_irq = false;
    if (radio.startTransmit(next->frame, next->len) == RADIOLIB_ERR_NONE) {
        s_transmitting = true;
        s_tx_started = millis();
        if (!next->relay) messages::set_status(next->id, MSG_SENT);
    } else {
        start_rx();
    }
    next->used = false;
}

static void service_retries()
{
    for (auto &p : s_pending) {
        if (!p.used || (int32_t)(millis() - p.next_ms) < 0) continue;
        if (p.tries >= 3) {
            p.used = false;
            messages::set_status(p.id, MSG_FAILED);
            continue;
        }
        p.tries++;
        p.next_ms = millis() + 20000 + p.tries * 10000;
        enqueue(p.frame, p.len, 0, false);
    }
}

static void service_periodic()
{
    static uint32_t next_nodeinfo = 15000, next_position = 60000, next_telemetry = 120000;
    uint32_t now = millis();
    if ((int32_t)(now - next_nodeinfo) >= 0) {
        send_nodeinfo(BROADCAST, false);
        next_nodeinfo = now + 3 * 3600 * 1000UL;
    }
    if ((int32_t)(now - next_position) >= 0) {
        if (settings.share_position && s_has_pos) send_position();
        next_position = now + 15 * 60 * 1000UL;
    }
    if ((int32_t)(now - next_telemetry) >= 0) {
        send_telemetry();
        next_telemetry = now + 30 * 60 * 1000UL;
    }
}

bool init()
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    s_our_num = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) | ((uint32_t)mac[4] << 8) | mac[5];
    if (s_our_num < 4 || s_our_num == BROADCAST) s_our_num = 4 + (crypto::random32() % 0x7FFFFFF0);
    s_radio_ok = configure_radio();
    return s_radio_ok;
}

void apply_settings()
{
    radio.standby();
    s_radio_ok = configure_radio();
}

void loop()
{
    if (!s_radio_ok) return;
    service_radio();
    service_retries();
    service_periodic();
    nodedb::save_if_dirty();
    messages::save_if_dirty();
}

bool radio_ok() { return s_radio_ok; }
uint32_t our_num() { return s_our_num; }
const Stats &stats() { return s_stats; }
void set_text_callback(text_cb_t cb) { s_text_cb = cb; }

void set_own_position(int32_t lat_i, int32_t lon_i, int32_t alt, uint8_t sats)
{
    s_lat = lat_i;
    s_lon = lon_i;
    s_alt = alt;
    s_sats = sats;
    s_has_pos = true;
}

bool own_position(int32_t *lat_i, int32_t *lon_i)
{
    if (!s_has_pos) return false;
    *lat_i = s_lat;
    *lon_i = s_lon;
    return true;
}

} // namespace mesh
