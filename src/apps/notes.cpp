#include "apps.h"
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <LittleFS.h>
#include <time.h>
#include <vector>
#include "core/storage.h"
#include "ui/theme.h"

static lv_obj_t *s_root, *s_status = nullptr, *s_box = nullptr, *s_text = nullptr;
static bool s_editing = false;
static String s_path, s_buf;

static fs::FS &store() { return sd_ok() ? (fs::FS &)SD : (fs::FS &)LittleFS; }

static void show_list();

static void save()
{
    if (!s_editing) return;
    if (!s_buf.length()) {
        // An empty note is not worth keeping
        if (store().exists(s_path)) store().remove(s_path);
        return;
    }
    File f = store().open(s_path, "w");
    if (!f) { ui_toast("Save failed"); return; }
    f.print(s_buf);
    f.close();
}

// The editor is the Keyboard app's typing screen with a file behind it: a status line, a
// bordered box showing the buffer with a cursor mark, keys taken straight from the app hook.
// No text area, so nothing blinks or re-lays out while typing.
static void redraw()
{
    if (!s_text) return;
    lv_label_set_text_fmt(s_text, "%s_", s_buf.c_str());
    lv_label_set_text_fmt(s_status, "%s   %u chars   (long-click = save)", s_path.c_str() + 7, (unsigned)s_buf.length());
    lv_obj_update_layout(s_box);
    lv_obj_scroll_to_y(s_box, LV_COORD_MAX, LV_ANIM_OFF);
}

static void edit(const String &path)
{
    s_editing = true;
    s_path = path;
    s_buf = "";
    File f = store().open(path, "r");
    if (f) { s_buf = f.readString(); f.close(); }
    lv_obj_clean(s_root);
    lv_group_remove_all_objs(ui_group());

    s_status = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_status, T_DIM, 0);
    lv_obj_set_width(s_status, LV_PCT(100));
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_CLIP);

    s_box = lv_obj_create(s_root);
    lv_obj_set_size(s_box, LV_PCT(100), 194);
    lv_obj_set_y(s_box, 18);
    lv_obj_set_style_bg_color(s_box, T_BG, 0);
    lv_obj_set_style_border_color(s_box, T_DIM, 0);
    lv_obj_set_style_border_width(s_box, 1, 0);
    lv_obj_set_style_radius(s_box, 2, 0);
    lv_obj_set_style_pad_all(s_box, 4, 0);
    s_text = lv_label_create(s_box);
    lv_label_set_long_mode(s_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_text, LV_PCT(100));
    lv_obj_set_style_text_font(s_text, t_font_mono, 0);
    redraw();
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
    s_status = s_box = s_text = nullptr;
    lv_obj_clean(s_root);
    lv_group_remove_all_objs(ui_group());
    if (!store().exists("/notes")) store().mkdir("/notes");

    lv_obj_t *list = lv_list_create(s_root);
    lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
    ui_list_text(list, sd_ok() ? "Notes on SD card  (n = new)" : "Notes in internal flash, no SD  (n = new)");
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
    if (!s_editing) {
        if (k == 'n') { new_note(); return true; }
        return false;
    }
    if (k == LV_KEY_ESC) return false; // on_back saves and leaves
    if (k == LV_KEY_PREV || k == LV_KEY_NEXT) {
        lv_obj_scroll_by_bounded(s_box, 0, k == LV_KEY_PREV ? 40 : -40, LV_ANIM_OFF);
        return true;
    }
    if (k == LV_KEY_BACKSPACE) { if (s_buf.length()) s_buf.remove(s_buf.length() - 1); }
    else if (k == LV_KEY_ENTER) s_buf += '\n';
    else if (k >= 0x20 && k < 0x7F) { if (s_buf.length() < 8000) s_buf += (char)k; }
    else return true; // other navigation keys mean nothing in a note
    redraw();
    return true;
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
    s_status = s_box = s_text = nullptr;
}

const App app_notes = {"Notes", LV_SYMBOL_EDIT, open, close, nullptr, on_back};
