#include "apps.h"
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <LittleFS.h>
#include <time.h>
#include <vector>
#include "core/storage.h"

static lv_obj_t *s_root, *s_ta = nullptr;
static bool s_editing = false;
static String s_path;

static fs::FS &store() { return sd_ok() ? (fs::FS &)SD : (fs::FS &)LittleFS; }

static void show_list();

static void save()
{
    if (!s_ta) return;
    const char *txt = lv_textarea_get_text(s_ta);
    if (!txt[0]) {
        // An empty note is not worth keeping
        if (store().exists(s_path)) store().remove(s_path);
        return;
    }
    File f = store().open(s_path, "w");
    if (!f) { ui_toast("Save failed"); return; }
    f.print(txt);
    f.close();
}

static void edit(const String &path)
{
    s_editing = true;
    s_path = path;
    lv_obj_clean(s_root);
    lv_group_remove_all_objs(ui_group());
    lv_obj_t *title = lv_label_create(s_root);
    lv_label_set_text_fmt(title, "%s   (long-click to save)", path.c_str());
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    s_ta = lv_textarea_create(s_root);
    lv_obj_set_size(s_ta, LV_PCT(100), 196);
    lv_obj_set_y(s_ta, 16);
    lv_textarea_set_max_length(s_ta, 8000);
    File f = store().open(path, "r");
    if (f) {
        String body = f.readString();
        f.close();
        lv_textarea_set_text(s_ta, body.c_str());
    }
    lv_group_add_obj(ui_group(), s_ta);
    lv_group_focus_obj(s_ta);
}

static void new_note()
{
    char name[48];
    time_t now = time(nullptr);
    if (now > 1700000000) {
        struct tm t;
        localtime_r(&now, &t);
        strftime(name, sizeof(name), "/notes/%Y-%m-%d_%H%M%S.txt", &t);
    } else {
        snprintf(name, sizeof(name), "/notes/note_%lu.txt", (unsigned long)(millis() / 1000));
    }
    edit(name);
}

static void note_clicked(lv_event_t *e)
{
    String *p = (String *)lv_event_get_user_data(e);
    edit(*p);
}

static std::vector<String> s_names;

static void show_list()
{
    s_editing = false;
    s_ta = nullptr;
    lv_obj_clean(s_root);
    lv_group_remove_all_objs(ui_group());
    if (!store().exists("/notes")) store().mkdir("/notes");

    lv_obj_t *list = lv_list_create(s_root);
    lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
    lv_list_add_text(list, sd_ok() ? "Notes on SD card  (n = new)" : "Notes in internal flash, no SD  (n = new)");
    lv_obj_t *nb = ui_list_btn(list, LV_SYMBOL_PLUS, "New note");
    lv_obj_add_event_cb(nb, [](lv_event_t *) { new_note(); }, LV_EVENT_CLICKED, nullptr);

    s_names.clear();
    File dir = store().open("/notes");
    while (File f = dir.openNextFile()) {
        if (!f.isDirectory()) s_names.push_back(String("/notes/") + f.name());
        f.close();
    }
    dir.close();
    // Names start with a timestamp, so reverse order is newest first
    std::sort(s_names.begin(), s_names.end(), [](const String &a, const String &b) { return a > b; });
    for (auto &p : s_names) {
        // First line of the note as its title
        String title = p.substring(7);
        File f = store().open(p, "r");
        if (f) {
            String first = f.readStringUntil('\n');
            f.close();
            first.trim();
            if (first.length()) title = first.substring(0, 40);
        }
        lv_obj_t *b = ui_list_btn(list, LV_SYMBOL_FILE, title.c_str());
        lv_obj_add_event_cb(b, note_clicked, LV_EVENT_CLICKED, &p);
    }
}

static bool keys(uint32_t k)
{
    if (!s_editing && k == 'n') { new_note(); return true; }
    return false;
}

static void open(lv_obj_t *parent)
{
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    sd_mount();
    ui_set_app_key_hook(keys);
    show_list();
}

static bool on_back()
{
    if (s_editing) {
        save();
        show_list();
        return true;
    }
    return false;
}

static void close()
{
    if (s_editing) save();
    s_editing = false;
    s_ta = nullptr;
}

const App app_notes = {"Notes", LV_SYMBOL_EDIT, open, close, nullptr, on_back};
