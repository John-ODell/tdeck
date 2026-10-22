#include "apps.h"
#include <Arduino.h>
#include <SD.h>
#include <vector>
#include "core/storage.h"

static lv_obj_t *s_root;
static String s_dir = "/";
static bool s_viewing = false;
struct Entry { String name; bool dir; size_t size; };
static std::vector<Entry> s_entries;
static int s_confirm = -1;   // index awaiting delete confirmation
static int s_focused = -1;

static void show_dir(const String &dir);

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
    lv_group_remove_all_objs(ui_group());
    lv_obj_t *page = lv_obj_create(s_root);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_t *l = lv_label_create(page);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    File f = SD.open(path, "r");
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

static void entry_clicked(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    Entry &en = s_entries[i];
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
    if (s_viewing || s_focused < 0 || s_focused >= (int)s_entries.size()) return false;
    if (k != 'd') return false;
    Entry &en = s_entries[s_focused];
    if (s_confirm != s_focused) {
        s_confirm = s_focused;
        ui_toast(("Press d again to delete " + en.name).c_str());
        return true;
    }
    String p = join(s_dir, en.name);
    bool ok = en.dir ? SD.rmdir(p) : SD.remove(p);
    ui_toast(ok ? "Deleted" : (en.dir ? "Folder must be empty" : "Delete failed"));
    show_dir(s_dir);
    return true;
}

static void show_dir(const String &dir)
{
    s_viewing = false;
    s_dir = dir;
    s_confirm = s_focused = -1;
    lv_obj_clean(s_root);
    lv_group_remove_all_objs(ui_group());
    ui_set_app_key_hook(keys);

    lv_obj_t *list = lv_list_create(s_root);
    lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
    if (!sd_mount()) {
        lv_list_add_text(list, "No SD card. Insert a FAT32/exFAT card and reopen.");
        return;
    }
    char a[16], b[16], hdr[96];
    snprintf(hdr, sizeof(hdr), "%s   %s used of %s   (d = delete)", dir.c_str(),
             fmt_size(sd_used_bytes(), a, sizeof(a)), fmt_size(sd_total_bytes(), b, sizeof(b)));
    lv_list_add_text(list, hdr);

    s_entries.clear();
    File d = SD.open(dir);
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
        lv_obj_add_event_cb(btn, entry_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, entry_focused, LV_EVENT_FOCUSED, (void *)(intptr_t)i);
    }
    if (s_entries.empty()) lv_list_add_text(list, "(empty)");
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
    if (s_viewing) { show_dir(s_dir); return true; }
    if (s_dir != "/") {
        int slash = s_dir.lastIndexOf('/');
        show_dir(slash <= 0 ? "/" : s_dir.substring(0, slash));
        return true;
    }
    return false;
}

static void close() { s_viewing = false; }

const App app_files = {"Files", LV_SYMBOL_DIRECTORY, open, close, nullptr, on_back};
