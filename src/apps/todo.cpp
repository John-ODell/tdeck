// To-do: a check list in /todo.txt, one "[ ] item" or "[x] item" per line, so the same
// file is editable on the computer. With the dongle online the list is kept in step with
// tdeck/todo.txt on the GEEK's card: opening the app pulls and merges, every change pushes.
#include "apps.h"
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <LittleFS.h>
#include <vector>
#include <algorithm>
#include "core/storage.h"
#include "link/companion.h"
#include "link/link.h"

static const char *LOCAL = "/todo.txt";
static const char *PULLED = "/todo.remote.txt";
static const char *REMOTE = "tdeck/todo.txt";

struct Item { String text; bool done; };
static std::vector<Item> s_items;
static lv_obj_t *s_root, *s_ta = nullptr;
static void add_item();
static bool s_adding = false, s_open = false;
static int s_focused = -1, s_confirm = -1;
static lv_timer_t *s_push_timer = nullptr;
static bool s_dirty = false;      // a change not yet on the card

static fs::FS &store() { return sd_ok() ? (fs::FS &)SD : (fs::FS &)LittleFS; }

static void show_list();

// ---- the file ---------------------------------------------------------------------

static void parse(const String &body, std::vector<Item> &out)
{
    int start = 0;
    while (start < (int)body.length()) {
        int nl = body.indexOf('\n', start);
        if (nl < 0) nl = body.length();
        String line = body.substring(start, nl);
        start = nl + 1;
        line.trim();
        if (!line.length()) continue;
        Item it = {line, false};
        if (line.startsWith("[x] ") || line.startsWith("[X] ")) { it.done = true; it.text = line.substring(4); }
        else if (line.startsWith("[ ] ")) it.text = line.substring(4);
        else if (line.startsWith("[x]") || line.startsWith("[X]")) { it.done = true; it.text = line.substring(3); }
        else if (line.startsWith("[]") || line.startsWith("[ ]")) it.text = line.substring(line.indexOf(']') + 1);
        it.text.trim();
        if (it.text.length()) out.push_back(it);
    }
}

static void load()
{
    s_items.clear();
    File f = store().open(LOCAL, "r");
    if (!f) return;
    parse(f.readString(), s_items);
    f.close();
}

static void save()
{
    File f = store().open(LOCAL, "w");
    if (!f) { ui_toast("Save failed"); return; }
    for (auto &it : s_items) f.printf("[%c] %s\n", it.done ? 'x' : ' ', it.text.c_str());
    f.close();
}

// The computer's copy folded in: new items are added, an item done on either side is done
static void merge_pulled()
{
    std::vector<Item> theirs;
    File f = store().open(PULLED, "r");
    if (!f) return;
    parse(f.readString(), theirs);
    f.close();
    store().remove(PULLED);
    for (auto &t : theirs) {
        bool found = false;
        for (auto &m : s_items)
            if (m.text == t.text) { m.done = m.done || t.done; found = true; break; }
        if (!found) s_items.push_back(t);
    }
}

// ---- sync ---------------------------------------------------------------------------

// Sync feedback only when something is wrong; a working sync is silent
static void trouble(const char *status)
{
    if (s_open && status && *status) ui_toast(status);
}

static void push_done(bool ok)
{
    if (ok) s_dirty = false;
    else trouble(companion::status());
}

static bool push()
{
    if (!dongle::online()) return false;
    if (!companion::fs_put(LOCAL, REMOTE, push_done)) return false;
    set_hdr("pushing");
    return true;
}

// Changes go to the card a moment after the last one, so a burst of ticks is one push
static void push_tick(lv_timer_t *)
{
    if (!s_dirty) { lv_timer_del(s_push_timer); s_push_timer = nullptr; return; }
    if (companion::busy()) return; // try again next tick
    if (!push()) { lv_timer_del(s_push_timer); s_push_timer = nullptr; }
}

static void changed()
{
    save();
    s_dirty = true;
    if (!dongle::paired()) return;
    if (!s_push_timer) s_push_timer = lv_timer_create(push_tick, 1500, nullptr);
    else lv_timer_reset(s_push_timer);
}

static void pull_done(bool ok)
{
    if (ok) {
        merge_pulled();
        save();
        if (!s_open) return; // the app closed while the pull was running
        if (!s_adding) show_list();
    }
    if (!s_open) return;
    // No file on the card yet is fine: ours becomes it
    if (!push()) trouble(companion::status());
}

static void sync_card()
{
    if (!dongle::online()) { ui_toast("Dongle is offline"); return; }
    if (companion::busy()) { ui_toast("Dongle is busy"); return; }
    if (!companion::fs_get(REMOTE, PULLED, pull_done)) { ui_toast("Dongle is busy"); return; }
    set_hdr("syncing");
}

// ---- the screen ---------------------------------------------------------------------

static void item_clicked(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= (int)s_items.size()) return;
    s_items[i].done = !s_items[i].done;
    changed();
    lv_obj_t *btn = lv_event_get_target(e);
    lv_label_set_text(lv_obj_get_child(btn, 0), s_items[i].done ? LV_SYMBOL_OK : LV_SYMBOL_STOP);
    lv_obj_set_style_text_opa(btn, s_items[i].done ? LV_OPA_50 : LV_OPA_COVER, 0);
}

static void item_focused(lv_event_t *e)
{
    s_focused = (int)(intptr_t)lv_event_get_user_data(e);
    s_confirm = -1;
}

static void show_list()
{
    s_adding = false;
    s_ta = nullptr;
    s_focused = s_confirm = -1;
    lv_obj_clean(s_root);
    lv_group_remove_all_objs(ui_group());

    lv_obj_t *list = lv_list_create(s_root);
    lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
    if (dongle::paired() && !dongle::online()) trouble("Dongle offline: changes stay here");
    for (size_t i = 0; i < s_items.size(); i++) {
        Item &it = s_items[i];
        lv_obj_t *btn = ui_list_btn(list, it.done ? LV_SYMBOL_OK : LV_SYMBOL_STOP, it.text.c_str());
        if (it.done) lv_obj_set_style_text_opa(btn, LV_OPA_50, 0);
        lv_obj_add_event_cb(btn, item_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, item_focused, LV_EVENT_FOCUSED, (void *)(intptr_t)i);
    }
    if (s_items.empty()) {
        lv_obj_t *add = ui_list_btn(list, LV_SYMBOL_PLUS, "Nothing to do. Add an item");
        lv_obj_add_event_cb(add, [](lv_event_t *) { add_item(); }, LV_EVENT_CLICKED, nullptr);
    }
}

static void add_item()
{
    s_adding = true;
    s_hdr = nullptr;
    lv_obj_clean(s_root);
    lv_group_remove_all_objs(ui_group());
    lv_obj_t *title = lv_label_create(s_root);
    lv_label_set_text(title, "New item   (Enter = add, long-click = cancel)");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    s_ta = lv_textarea_create(s_root);
    lv_obj_set_size(s_ta, LV_PCT(100), 60);
    lv_obj_set_y(s_ta, 16);
    lv_textarea_set_one_line(s_ta, true);
    lv_textarea_set_max_length(s_ta, 120);
    lv_group_add_obj(ui_group(), s_ta);
    lv_group_focus_obj(s_ta);
}

static bool keys(uint32_t k)
{
    if (s_adding) {
        if (k != LV_KEY_ENTER) return false;
        String text = lv_textarea_get_text(s_ta);
        text.trim();
        if (text.length()) { s_items.push_back({text, false}); changed(); }
        show_list();
        return true;
    }
    if (k == 'n') { add_item(); return true; }
    if (k == 's') { sync_card(); return true; }
    if (k == 'c') {
        size_t before = s_items.size();
        s_items.erase(std::remove_if(s_items.begin(), s_items.end(), [](const Item &i) { return i.done; }), s_items.end());
        if (s_items.size() != before) { changed(); show_list(); }
        else ui_toast("Nothing is done yet");
        return true;
    }
    if (k == 'd') {
        if (s_focused < 0 || s_focused >= (int)s_items.size()) return true;
        if (s_confirm != s_focused) {
            s_confirm = s_focused;
            ui_toast(("Press d again to delete: " + s_items[s_focused].text).c_str());
            return true;
        }
        s_items.erase(s_items.begin() + s_focused);
        changed();
        show_list();
        return true;
    }
    return false;
}

static void open(lv_obj_t *parent)
{
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    s_open = true;
    sd_mount();
    ui_set_app_key_hook(keys);
    load();
    show_list();
    if (dongle::online() && !companion::busy()) sync_card();
}

static bool on_back()
{
    if (s_adding) { show_list(); return true; }
    return false;
}

static void close()
{
    s_open = false;
    s_hdr = nullptr;
    s_ta = nullptr;
    s_adding = false;
    if (s_push_timer) { lv_timer_del(s_push_timer); s_push_timer = nullptr; }
    if (s_dirty && dongle::online() && !companion::busy()) push(); // the last change still goes out
}

const App app_todo = {"To-do", LV_SYMBOL_LIST, open, close, nullptr, on_back};
