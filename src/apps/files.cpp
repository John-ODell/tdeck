#include "apps.h"
#include <Arduino.h>
#include <SD.h>
#include <LittleFS.h>
#include <vector>
#include "core/storage.h"
#include "link/companion.h"
#include "link/link.h"

// Two drives: the T-Deck's own SD card, and the dongle's card over the link
static lv_obj_t *s_root;
static String s_dir = "/";
static bool s_viewing = false;
static bool s_remote = false;          // browsing the dongle's card
static lv_obj_t *s_status = nullptr;   // transfer progress line
static lv_timer_t *s_timer = nullptr;
static String s_pending_view;          // local path to open once a pull finishes
struct Entry { String name; bool dir; size_t size; };
static std::vector<Entry> s_entries;
static int s_confirm = -1;   // index awaiting delete confirmation
static int s_focused = -1;

static void show_dir(const String &dir);
static void show_remote(const String &dir);
static fs::FS &store() { return sd_ok() ? (fs::FS &)SD : (fs::FS &)LittleFS; } // pulled files land here

static String join(const String &dir, const String &name) { return dir == "/" ? "/" + name : dir + "/" + name; }

static const char *fmt_size(size_t n, char *buf, size_t cap)
{
    if (n < 1024) snprintf(buf, cap, "%u B", (unsigned)n);
    else if (n < 1024 * 1024) snprintf(buf, cap, "%.1f KB", n / 1024.0);
    else snprintf(buf, cap, "%.1f MB", n / 1048576.0);
    return buf;
}

static bool is_text(const String &n)
{
    String l = n;
    l.toLowerCase();
    return l.endsWith(".txt") || l.endsWith(".md") || l.endsWith(".csv") || l.endsWith(".log") ||
           l.endsWith(".json") || l.endsWith(".ini") || l.endsWith(".cfg");
}

static void view(const String &path)
{
    s_viewing = true;
    lv_obj_clean(s_root);
    s_status = nullptr;
    lv_group_remove_all_objs(ui_group());
    lv_obj_t *page = lv_obj_create(s_root);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_t *l = lv_label_create(page);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    File f = store().open(path, "r");
    String body = f ? f.readString().substring(0, 12000) : String("(cannot open)");
    if (f) f.close();
    lv_label_set_text(l, body.c_str());
    lv_group_add_obj(ui_group(), page);
    ui_set_app_key_hook([](uint32_t k) {
        if (!s_viewing) return false;
        lv_obj_t *p = lv_obj_get_child(s_root, 0);
        if (k == LV_KEY_PREV) { lv_obj_scroll_by_bounded(p, 0, 60, LV_ANIM_ON); return true; }
        if (k == LV_KEY_NEXT) { lv_obj_scroll_by_bounded(p, 0, -60, LV_ANIM_ON); return true; }
        return false;
    });
}

// Pulled files keep the card's layout under /dongle on the T-Deck
static String pulled_path(const String &remote) { return "/dongle" + remote; }

static void status_tick(lv_timer_t *)
{
    if (s_status) lv_label_set_text(s_status, companion::status());
    if (!companion::busy()) { lv_timer_del(s_timer); s_timer = nullptr; }
}

static void watch_transfer()
{
    if (!s_timer) s_timer = lv_timer_create(status_tick, 300, nullptr);
}

static void pull_done(bool ok)
{
    if (s_status) lv_label_set_text(s_status, companion::status());
    if (!ok) { ui_toast(companion::status()); s_pending_view = ""; return; }
    if (s_pending_view.length()) { String p = s_pending_view; s_pending_view = ""; view(p); }
    else ui_toast("Pulled to the T-Deck's /dongle folder");
}

static void entry_clicked(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (!s_remote && s_dir == "/" && i == 0 && dongle::paired()) { show_remote("/"); return; }
    if (!s_remote && s_dir == "/" && dongle::paired()) i--;
    Entry &en = s_entries[i];
    if (s_remote) {
        String path = join(s_dir, en.name);
        if (en.dir) { show_remote(path); return; }
        if (!is_text(en.name)) { ui_toast("Press g to pull it; only text opens here"); return; }
        s_pending_view = pulled_path(path);
        if (!companion::fs_get(path.c_str(), s_pending_view.c_str(), pull_done)) ui_toast("Dongle is busy or offline");
        else watch_transfer();
        return;
    }
    if (en.dir) show_dir(join(s_dir, en.name));
    else if (is_text(en.name)) view(join(s_dir, en.name));
    else ui_toast("Only text files can be opened");
}

static void entry_focused(lv_event_t *e)
{
    s_focused = (int)(intptr_t)lv_event_get_user_data(e);
    s_confirm = -1;
}

static bool keys(uint32_t k)
{
    if (s_viewing || s_focused < 0) return false;
    int idx = s_focused;
    if (!s_remote && s_dir == "/" && dongle::paired()) idx--; // the "Dongle card" row
    if (idx < 0 || idx >= (int)s_entries.size()) return false;
    Entry &en = s_entries[idx];
    if (s_remote) {
        if (k != 'g' || en.dir) return false;
        String path = join(s_dir, en.name);
        if (!companion::fs_get(path.c_str(), pulled_path(path).c_str(), pull_done)) ui_toast("Dongle is busy or offline");
        else watch_transfer();
        return true;
    }
    if (k == 'p' && !en.dir) {
        if (!dongle::paired()) { ui_toast("No dongle paired"); return true; }
        String remote = "tdeck/" + en.name;
        if (!companion::fs_put(join(s_dir, en.name).c_str(), remote.c_str(), [](bool ok) { ui_toast(ok ? "Pushed to the dongle's card" : companion::status()); }))
            ui_toast("Dongle is busy or offline");
        else watch_transfer();
        return true;
    }
    if (k != 'd') return false;
    if (s_confirm != s_focused) {
        s_confirm = s_focused;
        ui_toast(("Press d again to delete " + en.name).c_str());
        return true;
    }
    String p = join(s_dir, en.name);
    bool ok = en.dir ? store().rmdir(p) : store().remove(p);
    ui_toast(ok ? "Deleted" : (en.dir ? "Folder must be empty" : "Delete failed"));
    show_dir(s_dir);
    return true;
}

static void show_dir(const String &dir)
{
    s_viewing = false;
    s_remote = false;
    s_dir = dir;
    s_confirm = s_focused = -1;
    lv_obj_clean(s_root);
    s_status = nullptr;
    lv_group_remove_all_objs(ui_group());
    ui_set_app_key_hook(keys);

    lv_obj_t *list = lv_list_create(s_root);
    lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
    // Without a card the same browse, view, delete and push work on internal flash, where
    // pulled files land; the header says which store this is (#60)
    bool card = sd_mount();
    fs::FS &fs = store();
    char a[16], b[16], hdr[96];
    snprintf(hdr, sizeof(hdr), "%s%s   %s used of %s   (d = delete, p = push)", card ? "" : "flash:", dir.c_str(),
             fmt_size(card ? sd_used_bytes() : LittleFS.usedBytes(), a, sizeof(a)),
             fmt_size(card ? sd_total_bytes() : LittleFS.totalBytes(), b, sizeof(b)));
    ui_list_text(list, hdr);
    s_status = nullptr;
    if (dir == "/" && dongle::paired()) {
        // The dongle's card works with or without a local one
        lv_obj_t *btn = ui_list_btn(list, LV_SYMBOL_USB, dongle::online() ? "Dongle card" : "Dongle card (offline)");
        lv_obj_add_event_cb(btn, entry_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)0);
        lv_obj_add_event_cb(btn, entry_focused, LV_EVENT_FOCUSED, (void *)(intptr_t)0);
    }
    int base = dir == "/" && dongle::paired() ? 1 : 0;

    s_entries.clear();
    File d = fs.open(dir);
    while (d) {
        File f = d.openNextFile();
        if (!f) break;
        String n = f.name();
        int slash = n.lastIndexOf('/');
        if (slash >= 0) n = n.substring(slash + 1);
        s_entries.push_back({n, f.isDirectory(), f.isDirectory() ? 0 : (size_t)f.size()});
        f.close();
        if (s_entries.size() >= 200) break;
    }
    if (d) d.close();
    std::sort(s_entries.begin(), s_entries.end(), [](const Entry &x, const Entry &y) {
        if (x.dir != y.dir) return x.dir;
        return x.name < y.name;
    });
    for (size_t i = 0; i < s_entries.size(); i++) {
        Entry &en = s_entries[i];
        char line[96], sz[16];
        snprintf(line, sizeof(line), "%s%s", en.name.c_str(), en.dir ? "/" : (String("   ") + fmt_size(en.size, sz, sizeof(sz))).c_str());
        lv_obj_t *btn = ui_list_btn(list, en.dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE, line);
        lv_obj_add_event_cb(btn, entry_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)(i + base));
        lv_obj_add_event_cb(btn, entry_focused, LV_EVENT_FOCUSED, (void *)(intptr_t)(i + base));
    }
    if (s_entries.empty()) ui_list_text(list, "(empty)");
}

static void remote_listed(bool ok, const companion::FsEntry *e, size_t n)
{
    if (!s_remote) return;
    lv_obj_t *list = lv_obj_get_child(s_root, 0);
    if (!list) return;
    if (!ok) { lv_label_set_text(s_status, companion::status()); return; }
    lv_label_set_text(s_status, "");
    s_entries.clear();
    for (size_t i = 0; i < n; i++) s_entries.push_back({String(e[i].name), e[i].dir, e[i].size});
    std::sort(s_entries.begin(), s_entries.end(), [](const Entry &x, const Entry &y) {
        if (x.dir != y.dir) return x.dir;
        return x.name < y.name;
    });
    for (size_t i = 0; i < s_entries.size(); i++) {
        Entry &en = s_entries[i];
        char line[96], sz[16];
        snprintf(line, sizeof(line), "%s%s", en.name.c_str(), en.dir ? "/" : (String("   ") + fmt_size(en.size, sz, sizeof(sz))).c_str());
        lv_obj_t *btn = ui_list_btn(list, en.dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE, line);
        lv_obj_add_event_cb(btn, entry_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, entry_focused, LV_EVENT_FOCUSED, (void *)(intptr_t)i);
    }
    if (s_entries.empty()) ui_list_text(list, "(empty)");
}

// The dongle's card: the list arrives over the link a moment later
static void show_remote(const String &dir)
{
    s_viewing = false;
    s_remote = true;
    s_dir = dir;
    s_confirm = s_focused = -1;
    s_entries.clear();
    lv_obj_clean(s_root);
    s_status = nullptr;
    lv_group_remove_all_objs(ui_group());
    ui_set_app_key_hook(keys);
    lv_obj_t *list = lv_list_create(s_root);
    lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
    char hdr[96];
    snprintf(hdr, sizeof(hdr), "Dongle card %s   (click = open, g = pull)", dir.c_str());
    ui_list_text(list, hdr);
    s_status = ui_list_text(list, "listing...");
    if (!companion::fs_list(dir.c_str(), remote_listed)) lv_label_set_text(s_status, "Dongle is busy or offline");
}

static void open(lv_obj_t *parent)
{
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    show_dir(s_dir);
}

static bool on_back()
{
    if (s_viewing) { if (s_remote) show_remote(s_dir); else show_dir(s_dir); return true; }
    if (s_remote) {
        if (s_dir == "/") { show_dir("/"); return true; }
        int slash = s_dir.lastIndexOf('/');
        show_remote(slash <= 0 ? "/" : s_dir.substring(0, slash));
        return true;
    }
    if (s_dir != "/") {
        int slash = s_dir.lastIndexOf('/');
        show_dir(slash <= 0 ? "/" : s_dir.substring(0, slash));
        return true;
    }
    return false;
}

static void close()
{
    s_viewing = false;
    s_remote = false;
    s_status = nullptr;
    if (s_timer) { lv_timer_del(s_timer); s_timer = nullptr; }
}

const App app_files = {"Files", LV_SYMBOL_DIRECTORY, open, close, nullptr, on_back};
