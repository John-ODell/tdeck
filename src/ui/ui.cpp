#include "ui.h"
#include <Arduino.h>
#include <time.h>
#include "hal/input.h"
#include "hal/power.h"
#include "theme.h"

static const App *s_apps[24];
static int s_app_count = 0;
static const App *s_current = nullptr;
static lv_obj_t *s_screen, *s_status, *s_status_left, *s_status_right, *s_content;
static lv_group_t *s_group = nullptr;
static lv_obj_t *s_grid = nullptr;
static char s_extra[48] = "";
static ui_key_hook_t s_app_hook = nullptr;

void ui_set_app_key_hook(ui_key_hook_t hook) { s_app_hook = hook; }

static const int STATUS_H = 20;

// Launcher: left/right step one tile, up/down jump a whole row of the grid. Nothing wraps,
// so a long roll stops at the edge instead of landing on About.
static bool launcher_nav(uint32_t key)
{
    if (!s_grid || (key != LV_KEY_LEFT && key != LV_KEY_RIGHT && key != LV_KEY_PREV && key != LV_KEY_NEXT)) return false;
    int n = lv_obj_get_child_cnt(s_grid);
    if (!n) return false;
    lv_obj_t *f = lv_group_get_focused(s_group);
    int i = f && lv_obj_get_parent(f) == s_grid ? lv_obj_get_index(f) : 0;
    int cols = 1;
    lv_coord_t y0 = lv_obj_get_y(lv_obj_get_child(s_grid, 0));
    while (cols < n && lv_obj_get_y(lv_obj_get_child(s_grid, cols)) == y0) cols++;
    switch (key) {
    case LV_KEY_LEFT: i = i > 0 ? i - 1 : i; break;        // the ends stop, nothing wraps
    case LV_KEY_RIGHT: i = i < n - 1 ? i + 1 : i; break;
    case LV_KEY_PREV: i = i - cols >= 0 ? i - cols : i; break;
    case LV_KEY_NEXT: i = i + cols < n ? i + cols : (i / cols < (n - 1) / cols ? n - 1 : i); break;
    }
    lv_group_focus_obj(lv_obj_get_child(s_grid, i));
    return true;
}

static bool key_hook(uint32_t key)
{
    if (s_app_hook && s_app_hook(key)) return true;
    if (!s_current && launcher_nav(key)) return true;
    if (key != LV_KEY_ESC) return false;
    if (s_current && s_current->on_back && s_current->on_back()) return true;
    if (s_current) { ui_go_home(); return true; }
    return false;
}

static void new_group()
{
    if (s_group) lv_group_del(s_group);
    s_group = lv_group_create();
    lv_group_set_wrap(s_group, true);
    input_set_group(s_group);
}

static void clear_content()
{
    s_grid = nullptr;
    lv_obj_clean(s_content);
    lv_obj_set_style_pad_all(s_content, 4, 0); // apps like Map change it
    lv_obj_scroll_to(s_content, 0, 0, LV_ANIM_OFF);
}

static void launcher_clicked(lv_event_t *e)
{
    ui_open_app((const App *)lv_event_get_user_data(e));
}

static void build_launcher()
{
    clear_content();
    new_group();
    lv_obj_t *grid = lv_obj_create(s_content);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, 8, 0);
    lv_obj_set_style_pad_top(grid, 6, 0);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    s_grid = grid;

    for (int i = 0; i < s_app_count; i++) {
        lv_obj_t *btn = lv_btn_create(grid);
        lv_obj_set_size(btn, 92, 62);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(btn, 2, 0);
        lv_obj_t *icon = lv_label_create(btn);
        lv_label_set_text(icon, s_apps[i]->symbol);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, s_apps[i]->name);
        // The proportional font fits every name in the tile; the mono font is twice as wide
        // and had the long names ("Compass", "Spectrum", "Keyboard") rolling mid-word
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_width(label, 86);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_add_event_cb(btn, launcher_clicked, LV_EVENT_CLICKED, (void *)s_apps[i]);
        lv_group_add_obj(s_group, btn);
        lv_obj_add_event_cb(btn, [](lv_event_t *e) {
            lv_obj_scroll_to_view(lv_event_get_target(e), LV_ANIM_ON);
        }, LV_EVENT_FOCUSED, nullptr);
    }
}

void ui_init()
{
    s_screen = lv_scr_act();

    s_status = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_status);
    lv_obj_set_size(s_status, LV_PCT(100), STATUS_H);
    lv_obj_set_style_bg_color(s_status, T_SURFACE, 0);
    lv_obj_set_style_bg_opa(s_status, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(s_status, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(s_status, T_DIM, 0);
    lv_obj_set_style_border_width(s_status, 1, 0);
    lv_obj_set_style_pad_hor(s_status, 6, 0);
    lv_obj_clear_flag(s_status, LV_OBJ_FLAG_SCROLLABLE);

    s_status_left = lv_label_create(s_status);
    lv_obj_align(s_status_left, LV_ALIGN_LEFT_MID, 0, 0);
    s_status_right = lv_label_create(s_status);
    lv_obj_align(s_status_right, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_text_font(s_status_left, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_font(s_status_right, &lv_font_montserrat_12, 0);

    s_content = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_size(s_content, LV_PCT(100), LV_VER_RES - STATUS_H);
    lv_obj_set_y(s_content, STATUS_H);
    lv_obj_set_style_pad_all(s_content, 4, 0);

    input_set_key_hook(key_hook);
    ui_status_refresh();
    build_launcher();
    theme_rain_splash(1500);
}

void ui_register_app(const App *app)
{
    if (s_app_count < 24) s_apps[s_app_count++] = app;
}

void ui_open_app(const App *app)
{
    if (s_current && s_current->close) s_current->close();
    s_current = app;
    s_app_hook = nullptr;
    clear_content();
    new_group();
    app->open(s_content);
    ui_status_refresh();
}

void ui_go_home()
{
    if (s_current && s_current->close) s_current->close();
    s_current = nullptr;
    s_app_hook = nullptr;
    build_launcher();
    ui_status_refresh();
}

const App *ui_current_app() { return s_current; }

bool ui_open_app_named(const char *name)
{
    for (int i = 0; i < s_app_count; i++)
        if (!strcasecmp(s_apps[i]->name, name)) { ui_open_app(s_apps[i]); return true; }
    return false;
}
lv_group_t *ui_group() { return s_group; }

void ui_set_status_extra(const char *text)
{
    strlcpy(s_extra, text ? text : "", sizeof(s_extra));
    ui_status_refresh();
}

void ui_status_refresh()
{
    lv_label_set_text_fmt(s_status_left, "%s  %s", s_current ? s_current->name : "T-Deck", s_extra);
    char clock[8] = "--:--";
    time_t now = time(nullptr);
    if (now > 1700000000) {
        struct tm t;
        localtime_r(&now, &t);
        strftime(clock, sizeof(clock), "%H:%M", &t);
    }
    uint8_t pct = power_battery_percent();
    const char *bat = power_usb_powered() ? LV_SYMBOL_CHARGE
                      : pct > 80 ? LV_SYMBOL_BATTERY_FULL
                      : pct > 55 ? LV_SYMBOL_BATTERY_3
                      : pct > 30 ? LV_SYMBOL_BATTERY_2
                      : pct > 10 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
    lv_label_set_text_fmt(s_status_right, "%s  %s %d%%", clock, bat, pct);
}

void ui_tick()
{
    static uint32_t last = 0;
    if (millis() - last > 5000) {
        last = millis();
        ui_status_refresh();
    }
    if (s_current && s_current->tick) s_current->tick();
}

void ui_toast(const char *text)
{
    lv_obj_t *box = lv_label_create(lv_layer_top());
    lv_label_set_text(box, text);
    lv_label_set_long_mode(box, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(box, 280);
    lv_obj_set_style_bg_color(box, T_SURFACE_HI, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_90, 0);
    lv_obj_set_style_border_color(box, T_ACCENT, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_pad_all(box, 8, 0);
    lv_obj_set_style_radius(box, 2, 0);
    lv_obj_align(box, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_del_delayed(box, 2500);
}

// List labels wrap in the proportional font instead of scrolling: the theme's mono font is
// twice as wide and would set most rows rolling
static void settle_label(lv_obj_t *label)
{
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
}

lv_obj_t *ui_list_btn(lv_obj_t *list, const char *symbol, const char *text)
{
    lv_obj_t *btn = lv_list_add_btn(list, symbol, text);
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(btn); i++) {
        lv_obj_t *c = lv_obj_get_child(btn, i);
        if (lv_obj_check_type(c, &lv_label_class)) settle_label(c);
    }
    lv_group_add_obj(ui_group(), btn);
    return btn;
}

lv_obj_t *ui_list_text(lv_obj_t *list, const char *text)
{
    lv_obj_t *l = lv_list_add_text(list, text);
    settle_label(l);
    return l;
}
