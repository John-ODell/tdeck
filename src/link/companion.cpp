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

// ---- transfers -----------------------------------------------------------------
// One job at a time. Pushes send FILE_BEGIN/DATA/END and wait for the dongle's ACK on each
// blob; lists and gets send one request and then receive, ACKing as the link does.

enum Job { JOB_NONE, JOB_PUSH, JOB_LIST, JOB_GET };
enum Step { ST_NEXT_FILE, ST_DATA, ST_END, ST_FINISH, ST_WAIT, ST_RECEIVE };
static Job s_job = JOB_NONE;
static Step s_step = ST_NEXT_FILE, s_after = ST_NEXT_FILE;
static char s_status[48] = "";
static uint32_t s_started = 0;

struct PushItem { String local, remote; };
static std::vector<PushItem> s_push;
static size_t s_push_i = 0, s_pushed = 0;
static File s_fh;
static uint8_t s_wait_id = 0;
static int s_retries = 0;
static uint32_t s_sent_at = 0;
static uint8_t s_chunk[LINK_BLOB_MAX * LINK_BLOB_PARTS];
static size_t s_chunk_len = 0;
static uint8_t s_chunk_kind = 0;

static std::vector<FsEntry> s_entries;
static fs_list_cb_t s_list_cb = nullptr;
static fs_done_cb_t s_done_cb = nullptr;
static String s_get_local;
static uint32_t s_get_size = 0, s_get_got = 0;

static void set_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_status, sizeof(s_status), fmt, ap);
    va_end(ap);
}

static void mkdirs(const String &path)
{
    // Every folder above the file
    for (int i = 1; i < (int)path.length(); i++)
        if (path[i] == '/') store().mkdir(path.substring(0, i));
}

static void finish(bool ok, const char *why = nullptr)
{
    if (s_fh) s_fh.close();
    Job j = s_job;
    s_job = JOB_NONE;
    if (!ok && why) set_status("failed: %s", why);
    if (j == JOB_LIST && s_list_cb) { fs_list_cb_t cb = s_list_cb; s_list_cb = nullptr; cb(ok, s_entries.data(), s_entries.size()); }
    if ((j == JOB_GET || j == JOB_PUSH) && s_done_cb) { fs_done_cb_t cb = s_done_cb; s_done_cb = nullptr; cb(ok); }
}

static bool send_step(uint8_t kind, const void *data, size_t len, Step after)
{
    memcpy(s_chunk, data, len);
    s_chunk_len = len;
    s_chunk_kind = kind;
    s_retries = 0;
    if (!dongle::send_blob(kind, s_chunk, len, &s_wait_id)) return false;
    s_sent_at = millis();
    s_after = after;
    s_step = ST_WAIT;
    return true;
}

static bool start_job(Job j)
{
    if (!dongle::online() || s_job != JOB_NONE) return false;
    s_job = j;
    s_started = millis();
    return true;
}

bool fs_put(const char *local, const char *remote, fs_done_cb_t cb)
{
    if (!start_job(JOB_PUSH)) return false;
    s_push.clear();
    s_push.push_back({String(local), String(remote)});
    s_push_i = s_pushed = 0;
    s_done_cb = cb;
    s_step = ST_NEXT_FILE;
    return true;
}

bool sync_notes()
{
    if (!start_job(JOB_PUSH)) return false;
    s_push.clear();
    File dir = store().open("/notes");
    if (dir) {
        for (File f = dir.openNextFile(); f; f = dir.openNextFile())
            if (!f.isDirectory()) {
                String n = f.name();
                int slash = n.lastIndexOf('/');
                if (slash >= 0) n = n.substring(slash + 1);
                s_push.push_back({"/notes/" + n, "tdeck/notes/" + n});
            }
    }
    s_push_i = s_pushed = 0;
    s_done_cb = nullptr;
    if (s_push.empty()) {
        s_job = JOB_NONE;
        set_status("no notes to sync");
        return true;
    }
    s_step = ST_NEXT_FILE;
    return true;
}

bool fs_list(const char *path, fs_list_cb_t cb)
{
    if (!start_job(JOB_LIST)) return false;
    s_entries.clear();
    s_list_cb = cb;
    s_step = ST_RECEIVE;
    set_status("listing");
    if (!dongle::send_blob(BLOB_FS_LIST, path, strlen(path) + 1)) { finish(false, "send"); return false; }
    return true;
}

bool fs_get(const char *remote, const char *local, fs_done_cb_t cb)
{
    if (!start_job(JOB_GET)) return false;
    s_get_local = local;
    s_get_size = s_get_got = 0;
    s_done_cb = cb;
    s_step = ST_RECEIVE;
    set_status("pulling");
    if (!dongle::send_blob(BLOB_FS_GET, remote, strlen(remote) + 1)) { finish(false, "send"); return false; }
    return true;
}

// Blobs from the dongle: text to send, folder listings, a file we asked for
static void on_blob(uint8_t kind, const uint8_t *data, size_t len)
{
    switch (kind) {
    case BLOB_SEND_TEXT: {
        if (len <= sizeof(uint32_t)) return;
        LinkSendText t = {};
        memcpy(&t, data, len > sizeof(t) ? sizeof(t) : len);
        t.text[sizeof(t.text) - 1] = 0;
        if (!mesh::send_text(t.to, t.text)) Serial.println("[companion] mesh send failed");
        return;
    }
    case BLOB_FS_ENTRIES: {
        if (s_job != JOB_LIST || len < sizeof(LinkFsEntries)) return;
        const LinkFsEntries *h = (const LinkFsEntries *)data;
        const uint8_t *p = data + sizeof(*h), *end = data + len;
        for (int i = 0; i < h->count && p + sizeof(LinkFsEntry) <= end; i++) {
            const LinkFsEntry *e = (const LinkFsEntry *)p;
            p += sizeof(*e);
            if (p + e->nlen > end) break;
            if (s_entries.size() < 200) {
                FsEntry fe;
                size_t n = e->nlen < sizeof(fe.name) - 1 ? e->nlen : sizeof(fe.name) - 1;
                memcpy(fe.name, p, n);
                fe.name[n] = 0;
                fe.size = e->size;
                fe.dir = e->dir;
                s_entries.push_back(fe);
            }
            p += e->nlen;
        }
        s_started = millis();
        if (h->last) finish(true);
        return;
    }
    case BLOB_FILE_BEGIN: {
        if (s_job != JOB_GET || len < sizeof(LinkFileBegin)) return;
        const LinkFileBegin *b = (const LinkFileBegin *)data;
        mkdirs(s_get_local);
        s_fh = store().open(s_get_local, "w");
        if (!s_fh) { finish(false, "can't write"); return; }
        s_get_size = b->size;
        s_get_got = 0;
        s_started = millis();
        return;
    }
    case BLOB_FILE_DATA:
        if (s_job != JOB_GET || !s_fh) return;
        s_fh.write(data, len);
        s_get_got += len;
        s_started = millis();
        set_status("pulling %lu%%", s_get_size ? (unsigned long)(s_get_got * 100 / s_get_size) : 0UL);
        return;
    case BLOB_FILE_END:
        if (s_job != JOB_GET) return;
        set_status("pulled");
        finish(true);
        return;
    case BLOB_FS_ERROR: {
        if (s_job != JOB_LIST && s_job != JOB_GET) return;
        char msg[40];
        size_t n = len < sizeof(msg) - 1 ? len : sizeof(msg) - 1;
        memcpy(msg, data, n);
        msg[n] = 0;
        finish(false, msg);
        return;
    }
    }
}

static void push_tick()
{
    switch (s_step) {
    case ST_NEXT_FILE: {
        if (s_push_i >= s_push.size()) { s_step = ST_FINISH; return; }
        s_fh = store().open(s_push[s_push_i].local, "r");
        if (!s_fh) { s_push_i++; return; }
        LinkFileBegin b = {};
        b.size = s_fh.size();
        // FAT keeps local time with no zone and the dongle knows no zone, so the epoch goes
        // shifted by our UTC offset: the card then shows the time the Notes app does (#59)
        time_t t = s_fh.getLastWrite();
        struct tm g;
        gmtime_r(&t, &g);
        g.tm_isdst = -1;
        b.mtime = t ? (uint32_t)(t + (t - mktime(&g))) : 0;
        strlcpy(b.name, s_push[s_push_i].remote.c_str(), sizeof(b.name));
        if (s_push.size() > 1) set_status("pushing %u/%u", (unsigned)(s_push_i + 1), (unsigned)s_push.size());
        else set_status("pushing");
        if (!send_step(BLOB_FILE_BEGIN, &b, sizeof(b), ST_DATA)) finish(false, "send");
        return;
    }
    case ST_DATA: {
        uint8_t buf[LINK_BLOB_MAX * LINK_BLOB_PARTS];
        size_t n = s_fh.read(buf, sizeof(buf));
        if (n == 0) { s_step = ST_END; return; }
        if (s_push.size() == 1 && s_fh.size()) set_status("pushing %lu%%", (unsigned long)(s_fh.position() * 100 / s_fh.size()));
        if (!send_step(BLOB_FILE_DATA, buf, n, ST_DATA)) finish(false, "send");
        return;
    }
    case ST_END: {
        s_fh.close();
        uint8_t z = 0;
        s_push_i++;
        s_pushed++;
        if (!send_step(BLOB_FILE_END, &z, 1, ST_NEXT_FILE)) finish(false, "send");
        return;
    }
    case ST_FINISH: {
        uint8_t z = 0;
        if (s_push.size() > 1) set_status("synced %u notes", (unsigned)s_pushed);
        else set_status(s_pushed ? "pushed" : "nothing to push");
        if (!send_step(BLOB_SYNC_END, &z, 1, ST_FINISH)) finish(false, "send"); // ST_FINISH after the ACK = done
        return;
    }
    case ST_WAIT: {
        bool ok;
        if (dongle::take_ack(s_wait_id, &ok)) {
            if (!ok) { finish(false, "dongle refused"); return; }
            if (s_after == ST_FINISH) { finish(true); return; }
            s_step = s_after;
            return;
        }
        if (millis() - s_sent_at > 1500) {
            if (++s_retries > 3) { finish(false, "no answer"); return; }
            dongle::send_blob(s_chunk_kind, s_chunk, s_chunk_len, &s_wait_id);
            s_sent_at = millis();
        }
        return;
    }
    default:
        return;
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
    if (!dongle::running()) {
        if (s_job != JOB_NONE) finish(false, "link off");
        return;
    }
    if (s_enabled) relay_tick();
    if (s_job == JOB_PUSH) push_tick();
    else if ((s_job == JOB_LIST || s_job == JOB_GET) && millis() - s_started > 4000) finish(false, "no answer");
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
bool busy() { return s_job != JOB_NONE; }

} // namespace companion
