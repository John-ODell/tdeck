#include "messages.h"
#include <Arduino.h>
#include <LittleFS.h>
#include "mesh/protocol.h"

namespace messages {

// Ring buffer in PSRAM, persisted whole (about 75 KB) with a debounce.
static Message *s_ring = nullptr;
static size_t s_start = 0, s_count = 0;
static bool s_dirty = false;
static uint32_t s_dirty_at = 0, s_version = 1;
static const char *PATH = "/msgs.bin";
static const uint32_t MAGIC = 0x4D534731; // "MSG1"

void load()
{
    if (!s_ring) s_ring = (Message *)ps_calloc(CAPACITY, sizeof(Message));
    if (!s_ring) s_ring = (Message *)calloc(CAPACITY, sizeof(Message)); // no PSRAM
    s_start = s_count = 0;
    File f = LittleFS.open(PATH, "r");
    if (!f) return;
    uint32_t magic = 0, n = 0;
    f.read((uint8_t *)&magic, 4);
    f.read((uint8_t *)&n, 4);
    if (magic == MAGIC && n <= CAPACITY && f.size() == 8 + n * sizeof(Message)) {
        f.read((uint8_t *)s_ring, n * sizeof(Message));
        s_count = n;
    }
    f.close();
}

static void dirty()
{
    if (!s_dirty) s_dirty_at = millis();
    s_dirty = true;
    s_version++;
}

void save_if_dirty()
{
    if (!s_dirty || millis() - s_dirty_at < 10000) return;
    save_now();
}

void save_now()
{
    if (!s_dirty) return;
    s_dirty = false;
    File f = LittleFS.open(PATH, "w");
    if (!f) return;
    uint32_t n = s_count;
    f.write((const uint8_t *)&MAGIC, 4);
    f.write((const uint8_t *)&n, 4);
    for (size_t i = 0; i < s_count; i++) f.write((const uint8_t *)at(i), sizeof(Message));
    f.close();
}

Message *at(size_t i) { return i < s_count ? &s_ring[(s_start + i) % CAPACITY] : nullptr; }
size_t count() { return s_count; }
uint32_t version() { return s_version; }

Message *add(const Message &m)
{
    Message *slot;
    if (s_count < CAPACITY) {
        slot = &s_ring[(s_start + s_count) % CAPACITY];
        s_count++;
    } else {
        slot = &s_ring[s_start];
        s_start = (s_start + 1) % CAPACITY;
    }
    *slot = m;
    dirty();
    return slot;
}

Message *find_sent(uint32_t id)
{
    for (size_t i = s_count; i-- > 0;) {
        Message *m = at(i);
        if (m->id == id && m->status != MSG_RECEIVED) return m;
    }
    return nullptr;
}

void set_status(uint32_t id, MsgStatus s)
{
    Message *m = find_sent(id);
    if (!m) return;
    // Never downgrade: a late ACK still upgrades a FAILED message
    static const uint8_t rank[] = {0, 1, 2, 4, 5, 3}; // RECEIVED QUEUED SENT RELAYED ACKED FAILED
    if (rank[s] <= rank[m->status]) return;
    m->status = s;
    dirty();
}

uint32_t peer_of(const Message &m, uint32_t our_num)
{
    if (m.to == mt::BROADCAST) return mt::BROADCAST;
    return m.from == our_num ? m.to : m.from;
}

size_t unread_count()
{
    size_t n = 0;
    for (size_t i = 0; i < s_count; i++) n += at(i)->unread;
    return n;
}

size_t unread_for(uint32_t peer, uint32_t our_num)
{
    size_t n = 0;
    for (size_t i = 0; i < s_count; i++) {
        Message *m = at(i);
        if (m->unread && peer_of(*m, our_num) == peer) n++;
    }
    return n;
}

void mark_read(uint32_t peer, uint32_t our_num)
{
    bool changed = false;
    for (size_t i = 0; i < s_count; i++) {
        Message *m = at(i);
        if (m->unread && peer_of(*m, our_num) == peer) { m->unread = false; changed = true; }
    }
    if (changed) dirty();
}

} // namespace messages
