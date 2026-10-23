#include "companion.h"
#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include <LittleFS.h>
#include <vector>
#include "link.h"
#include "link_proto.h"
#include "core/messages.h"
#include "core/nodedb.h"
#include "core/storage.h"
#include "core/settings.h"
#include "mesh/mesh.h"
#include "ui/ui.h"

namespace companion {

static bool s_enabled = false;
static size_t s_seen = 0;       // messages already forwarded
static uint32_t s_seen_id = 0;  // newest forwarded id, for when the store is full and rotates

static fs::FS &store() { return sd_ok() ? (fs::FS &)SD : (fs::FS &)LittleFS; }

// ---- mesh relay ----------------------------------------------------------------

static void forward(const Message &m)
{
    LinkTextEvent e = {};
    e.from = m.from;
    e.to = m.to;
    e.id = m.id;
    e.time = m.time;
    e.status = m.status;
    e.pki = m.pki;
    if (m.from == mesh::our_num()) strlcpy(e.name, settings.long_name[0] ? settings.long_name : settings.short_name, sizeof(e.name));
    else nodedb::display_name(m.from, e.name, sizeof(e.name));
    strlcpy(e.text, m.text, sizeof(e.text));
    size_t n = offsetof(LinkTextEvent, text) + strlen(e.text) + 1;
    dongle::send_blob(BLOB_TEXT_EVENT, &e, n);
}

static void on_blob(uint8_t kind, const uint8_t *data, size_t len)
{
    if (kind != BLOB_SEND_TEXT || len <= sizeof(uint32_t)) return;
    LinkSendText t = {};
    memcpy(&t, data, len > sizeof(t) ? sizeof(t) : len);
    t.text[sizeof(t.text) - 1] = 0;
    if (!mesh::send_text(t.to, t.text)) Serial.println("[companion] mesh send failed");
}

static void relay_tick()
{
    size_t n = messages::count();
    if (!dongle::online()) {
        // No backlog: whatever came in while the dongle was away stays on the T-Deck
        s_seen = n;
        if (n) s_seen_id = messages::at(n - 1)->id;
        return;
    }
    if (n > s_seen) {
        for (size_t i = s_seen; i < n; i++) forward(*messages::at(i));
    } else if (n == messages::CAPACITY && n && messages::at(n - 1)->id != s_seen_id) {
        forward(*messages::at(n - 1)); // store is full and rotated under us
    }
    s_seen = n;
    if (n) s_seen_id = messages::at(n - 1)->id;
}

// ---- notes sync ----------------------------------------------------------------

enum SyncStep { SYNC_IDLE, SYNC_BEGIN, SYNC_DATA, SYNC_END, SYNC_FINISH, SYNC_WAIT };
static SyncStep s_step = SYNC_IDLE, s_after = SYNC_IDLE;
static std::vector<String> s_files;
static size_t s_file = 0, s_sent_files = 0;
static File s_fh;
static uint8_t s_wait_id = 0;
static int s_retries = 0;
static uint32_t s_sent_at = 0;
static char s_status[48] = "";
static uint8_t s_chunk[LINK_BLOB_MAX * LINK_BLOB_PARTS];
static size_t s_chunk_len = 0;
static uint8_t s_chunk_kind = 0;

static void set_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_status, sizeof(s_status), fmt, ap);
    va_end(ap);
}

static bool send_step(uint8_t kind, const void *data, size_t len, SyncStep after)
{
    memcpy(s_chunk, data, len);
    s_chunk_len = len;
    s_chunk_kind = kind;
    s_retries = 0;
    if (!dongle::send_blob(kind, s_chunk, len, &s_wait_id)) return false;
    s_sent_at = millis();
    s_after = after;
    s_step = SYNC_WAIT;
    return true;
}

bool sync_notes()
{
    if (!dongle::online() || s_step != SYNC_IDLE) return false;
    s_files.clear();
    File dir = store().open("/notes");
    if (dir) {
        for (File f = dir.openNextFile(); f; f = dir.openNextFile())
            if (!f.isDirectory()) s_files.push_back(String("/notes/") + f.name());
    }
    s_file = s_sent_files = 0;
    if (s_files.empty()) {
        set_status("no notes to sync");
        return true;
    }
    s_step = SYNC_BEGIN;
    return true;
}

static void sync_fail(const char *why)
{
    if (s_fh) s_fh.close();
    s_step = SYNC_IDLE;
    set_status("sync failed: %s", why);
}

static void sync_tick()
{
    switch (s_step) {
    case SYNC_IDLE:
        return;
    case SYNC_BEGIN: {
        if (s_file >= s_files.size()) { s_step = SYNC_FINISH; return; }
        s_fh = store().open(s_files[s_file], "r");
        if (!s_fh) { s_file++; return; }
        LinkFileBegin b = {};
        b.size = s_fh.size();
        b.mtime = s_fh.getLastWrite();
        const char *name = strrchr(s_files[s_file].c_str(), '/');
        strlcpy(b.name, name ? name + 1 : s_files[s_file].c_str(), sizeof(b.name));
        set_status("syncing %u/%u", (unsigned)(s_file + 1), (unsigned)s_files.size());
        if (!send_step(BLOB_FILE_BEGIN, &b, sizeof(b), SYNC_DATA)) sync_fail("send");
        return;
    }
    case SYNC_DATA: {
        uint8_t buf[LINK_BLOB_MAX * LINK_BLOB_PARTS];
        size_t n = s_fh.read(buf, sizeof(buf));
        if (n == 0) { s_step = SYNC_END; return; }
        if (!send_step(BLOB_FILE_DATA, buf, n, SYNC_DATA)) sync_fail("send");
        return;
    }
    case SYNC_END: {
        s_fh.close();
        uint8_t z = 0;
        s_file++;
        s_sent_files++;
        if (!send_step(BLOB_FILE_END, &z, 1, SYNC_BEGIN)) sync_fail("send");
        return;
    }
    case SYNC_FINISH: {
        uint8_t z = 0;
        if (!send_step(BLOB_SYNC_END, &z, 1, SYNC_IDLE)) sync_fail("send");
        set_status("synced %u notes", (unsigned)s_sent_files);
        return;
    }
    case SYNC_WAIT: {
        bool ok;
        if (dongle::take_ack(s_wait_id, &ok)) {
            if (ok) { s_step = s_after; return; }
            sync_fail("dongle refused");
            return;
        }
        if (millis() - s_sent_at > 1500) {
            if (++s_retries > 3) { sync_fail("no answer"); return; }
            dongle::send_blob(s_chunk_kind, s_chunk, s_chunk_len, &s_wait_id);
            s_sent_at = millis();
        }
        return;
    }
    }
}

// ---- public ---------------------------------------------------------------------

void init()
{
    Preferences p;
    p.begin("kbd", false);
    s_enabled = p.getUChar("relay", 0) != 0;
    p.end();
    dongle::set_blob_cb(on_blob);
    s_seen = messages::count();
    if (s_seen) s_seen_id = messages::at(s_seen - 1)->id;
    if (s_enabled) {
        dongle::start(); // loads the pairing; nothing to relay to without one
        if (!dongle::paired()) dongle::stop();
    }
}

void tick()
{
    dongle::tick();
    if (!dongle::running()) return;
    if (s_enabled) relay_tick();
    sync_tick();
}

bool enabled() { return s_enabled; }

void set_enabled(bool on)
{
    s_enabled = on;
    Preferences p;
    p.begin("kbd", false);
    p.putUChar("relay", on);
    p.end();
    if (on) {
        dongle::start();
        s_seen = messages::count();
        if (s_seen) s_seen_id = messages::at(s_seen - 1)->id;
    }
}

const char *status() { return s_status; }

} // namespace companion
