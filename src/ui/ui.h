#pragma once
#include <lvgl.h>

struct App {
    const char *name;
    const char *symbol;                // LV_SYMBOL_* or short text
    void (*open)(lv_obj_t *parent);    // build the app inside parent
    void (*close)();                   // optional: free state (widgets are deleted for you)
    void (*tick)();                    // optional: called from loop while open
    bool (*on_back)();                 // optional: return true if ESC was handled inside the app
};

void ui_init();
void ui_tick();
void ui_register_app(const App *app);
void ui_open_app(const App *app);
bool ui_open_app_named(const char *name); // case-insensitive
void ui_go_home();
const App *ui_current_app();
lv_group_t *ui_group();           // group for the open app: add focusable widgets here
void ui_toast(const char *text);  // short notice over everything
void ui_set_status_extra(const char *text); // e.g. unread count
void ui_status_refresh();
void screen_wake();                 // turn the backlight on as if a key were pressed

// Per-app raw key hook (reset whenever an app opens). Return true to swallow.
typedef bool (*ui_key_hook_t)(uint32_t key);
void ui_set_app_key_hook(ui_key_hook_t hook);

// Shared styling helpers
lv_obj_t *ui_list_btn(lv_obj_t *list, const char *symbol, const char *text);
