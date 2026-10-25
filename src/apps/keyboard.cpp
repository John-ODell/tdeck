// Keyboard: the T-Deck as a wireless keyboard + mouse, over Bluetooth HID or the
// ESP-NOW USB dongle (dongle/). Live mode forwards every key; hold the trackball to stop.
#include "apps.h"
#include "core/settings.h"
#include "ui/theme.h"
#include <Arduino.h>
#include <Preferences.h>
#include "hal/hid_ble.h"
#include "hal/input.h"
#include "hal/power.h"
#include "link/link.h"
#include "link/companion.h"
#include "core/hid_keys.h"

enum Target { T_BLUETOOTH, T_DONGLE };
enum Ball { B_MOUSE, B_ARROWS };
static Target s_target = T_BLUETOOTH;
static Ball s_ball = B_MOUSE;

static lv_obj_t *s_setup, *s_live, *s_pairv;
static lv_obj_t *s_start_btn, *s_target_dd, *s_ball_dd, *s_relay_dd, *s_status, *s_pair_btn, *s_sync_btn, *s_forget_btn;
static lv_obj_t *s_term, *s_term_box, *s_live_status, *s_pair_list, *s_pair_note;
static bool s_in_live = false, s_in_pair = false, s_pairing = false;
static char s_echo[512];
static size_t s_echo_len = 0;
static size_t s_caret = 0;          // insert point in s_echo (#155)
static lv_obj_t *s_caret_obj;       // the drawn caret, a child of the text label
static int s_arrows_due = 0;        // Left (<0) or Right (>0) presses still to send so the computer's cursor follows a tap
static uint8_t s_buttons = 0;
static uint8_t s_pair_macs[4][6];
static size_t s_pair_count = (size_t)-1;

// ---- output ----------------------------------------------------------------------

static void out_tap(uint8_t mod, uint8_t usage)
{
    if (s_target == T_BLUETOOTH) hid_ble::tap(mod, usage);
    else dongle::tap(mod, usage);
}

static void out_mouse(uint8_t buttons, int dx, int dy, int wheel = 0)
{
    // Split big moves into int8 reports; the wheel goes with the first
    do {
        int8_t x = dx > 127 ? 127 : dx < -127 ? -127 : dx;
        int8_t y = dy > 127 ? 127 : dy < -127 ? -127 : dy;
        int8_t w = wheel > 127 ? 127 : wheel < -127 ? -127 : wheel;
        if (s_target == T_BLUETOOTH) hid_ble::mouse(buttons, x, y, w);
        else dongle::mouse(buttons, x, y, w);
        dx -= x;
        dy -= y;
        wheel = 0;
    } while (dx || dy);
}

// Mouse mode (#123): a short press is a click, holding the button and rolling is the scroll
// wheel, and a second press right after a release, held down, is a drag. Movement is
// collected in 30 ms frames so a slow roll moves a few pixels and a flick moves far.
// The trackball button (#141): a click goes out the moment the button is released. Holding
// it 300 ms, or rolling while it is down, is a drag (button held until release). A second
// click within 300 ms of the first toggles scroll mode and is swallowed, so the computer
// sees one click; in scroll mode the ball drives the wheel and a click leaves it.
static void update_status();
static bool s_scroll = false;
static void mouse_tick()
{
    enum { IDLE, DOWN, DRAG };
    static int state = IDLE;
    static uint32_t frame_at = 0, at = 0, click_at = 0;
    static int fdx = 0, fdy = 0, wheel_rem = 0;
    static bool was_down = false, rolled = false;
    int dx, dy;
    input_trackball_take(&dx, &dy);
    fdx += dx;
    fdy += dy;
    if (dx || dy) rolled = true;
    bool down = input_trackball_pressed();
    uint32_t now = millis();
    if (down && !was_down) {
        at = now;
        rolled = false;
        state = DOWN;
    } else if (!down && was_down) {
        if (state == DOWN) {
            if (s_scroll) { s_scroll = false; update_status(); }           // leave scroll mode, no click
            else if (click_at && now - click_at < 300) {                   // a double-click: scroll mode
                s_scroll = true;
                click_at = 0;
                update_status();
            } else {
                out_mouse(1, 0, 0);                                       // a click, right now
                out_mouse(0, 0, 0);
                click_at = now ? now : 1;
            }
        } else if (state == DRAG) {
            s_buttons = 0;
            out_mouse(0, 0, 0);
            if (s_target == T_DONGLE) out_mouse(0, 0, 0);                 // a lost release would leave it held
        }
        state = IDLE;
    }
    was_down = down;
    if (state == DOWN && !s_scroll && (rolled || now - at >= 300)) {       // held: a drag
        s_buttons = 1;
        out_mouse(1, 0, 0);
        state = DRAG;
    }
    if (now - frame_at < 30) return;
    frame_at = now;
    if (!fdx && !fdy) return;
    if (s_scroll) {
        // The wheel, two edges a notch, roll up = scroll up
        wheel_rem -= fdy;
        int w = wheel_rem / 2;
        wheel_rem -= w * 2;
        if (w) out_mouse(0, 0, 0, w);
        fdx = fdy = 0;
        return;
    }
    static const int BASE[] = {3, 4, 6, 8, 10};        // pixels per edge at a slow roll
    const int base = BASE[(settings.trackball >= 1 && settings.trackball <= 5 ? settings.trackball : 3) - 1];
    const int e = abs(fdx) + abs(fdy);
    int boost = 1 + e * e / 2;                          // 1 edge x1, 2 x3, 3 x5, 4 x9
    if (boost > 8) boost = 8;
    out_mouse(s_buttons, fdx * base * boost, fdy * base * boost);
    fdx = fdy = 0;
}

static bool out_ready() { return s_target == T_BLUETOOTH ? hid_ble::connected() : dongle::online(); }

static void apply_target()
{
    if (s_target == T_BLUETOOTH) {
        if (!companion::enabled()) dongle::stop(); // the relay keeps the link up in the background
        hid_ble::start();
        hid_ble::set_battery(power_battery_percent());
    } else {
        hid_ble::stop();
        dongle::start();
    }
}

static void save_prefs()
{
    Preferences p;
    p.begin("kbd", false);
    p.putUChar("target", s_target);
    p.putUChar("ball", s_ball);
    p.end();
}

// ---- views ---------------------------------------------------------------------

static void show(lv_obj_t *view)
{
    for (lv_obj_t *v : {s_setup, s_live, s_pairv}) {
        if (v == view) lv_obj_clear_flag(v, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(v, LV_OBJ_FLAG_HIDDEN);
    }
    // Only the visible view's widgets take focus
    lv_group_t *g = ui_group();
    lv_group_remove_all_objs(g);
    if (view == s_setup) {
        uint32_t n = lv_obj_get_child_cnt(s_setup);
        for (uint32_t i = 0; i < n; i++) {
            lv_obj_t *o = lv_obj_get_child(s_setup, i);
            if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) continue;
            if (lv_obj_check_type(o, &lv_btn_class)) { lv_group_add_obj(g, o); continue; }
            uint32_t m = lv_obj_get_child_cnt(o);
            for (uint32_t j = 0; j < m; j++) {
                lv_obj_t *c = lv_obj_get_child(o, j);
                if (lv_obj_check_type(c, &lv_dropdown_class) || lv_obj_check_type(c, &lv_btn_class)) lv_group_add_obj(g, c);
            }
        }
        lv_group_focus_obj(s_start_btn);
    } else if (view == s_pairv) {
        uint32_t n = lv_obj_get_child_cnt(s_pair_list);
        for (uint32_t i = 0; i < n; i++) lv_group_add_obj(g, lv_obj_get_child(s_pair_list, i));
    }
}

static void echo_redraw()
{
    lv_label_set_text(s_term, s_echo);
    lv_obj_update_layout(s_term);
    lv_point_t pos;
    lv_label_get_letter_pos(s_term, s_caret, &pos);
    lv_obj_set_pos(s_caret_obj, pos.x, pos.y);
    lv_obj_update_layout(s_term_box);
    lv_obj_scroll_to_view(s_caret_obj, LV_ANIM_OFF);
}

// The box mirrors the computer's text field as far as a keyboard can tell: typing inserts
// at the caret, backspace deletes before it, left and right move it (#155)
static void echo(uint8_t c)
{
    if (c == 0x08) {
        if (!s_caret) return;
        memmove(s_echo + s_caret - 1, s_echo + s_caret, s_echo_len - s_caret + 1);
        s_caret--;
        s_echo_len--;
    } else {
        if (s_echo_len + 2 >= sizeof(s_echo)) {
            // Keep the newer half
            size_t keep = s_echo_len / 2, drop = s_echo_len - keep;
            memmove(s_echo, s_echo + drop, keep + 1);
            s_echo_len = keep;
            s_caret = s_caret > drop ? s_caret - drop : 0;
        }
        memmove(s_echo + s_caret + 1, s_echo + s_caret, s_echo_len - s_caret + 1);
        s_echo[s_caret++] = c == '\r' ? '\n' : c;
        s_echo_len++;
    }
    echo_redraw();
}

static void caret_move(int d)
{
    if (d < 0 && s_caret < (size_t)-d) d = -(int)s_caret;
    if (d > 0 && s_caret + d > s_echo_len) d = s_echo_len - s_caret;
    s_caret += d;
    echo_redraw();
}

// A tap in the text puts the caret on that letter and queues the arrow presses that move the
// computer's cursor the same distance
static void term_clicked(lv_event_t *)
{
    lv_indev_t *in = lv_indev_get_act();
    if (!in) return;
    lv_point_t p;
    lv_indev_get_point(in, &p);
    lv_area_t a;
    lv_obj_get_coords(s_term, &a);
    p.x -= a.x1;
    p.y -= a.y1;
    uint32_t idx = lv_label_get_letter_on(s_term, &p);
    if (idx > s_echo_len) idx = s_echo_len;
    s_arrows_due += (int)idx - (int)s_caret;
    caret_move((int)idx - (int)s_caret);
}

static void enter_live()
{
    s_scroll = false;
    s_in_live = true;
    s_buttons = 0;
    s_arrows_due = 0;
    input_set_trackball_mode(s_ball == B_MOUSE ? TB_RAW : TB_ARROWS);
    show(s_live);
}

static void leave_live()
{
    if (s_buttons) out_mouse(0, 0, 0);
    s_buttons = 0;
    s_in_live = false;
    s_arrows_due = 0;
    input_set_trackball_mode(TB_FOCUS);
    show(s_setup);
}

static void enter_pair()
{
    s_in_pair = true;
    s_pairing = false;
    s_pair_count = (size_t)-1;
    dongle::pair_listen(true);
    lv_label_set_text(s_pair_note, "Plug the dongle into a computer. An unpaired dongle shows up here; "
                                   "to re-pair one, hold its BOOT button for 2 s.");
    show(s_pairv);
}

static void leave_pair()
{
    s_in_pair = false;
    dongle::pair_listen(false);
    show(s_setup);
}

// ---- keys ----------------------------------------------------------------------

static bool keys(uint32_t k)
{
    if (s_in_pair) {
        if (k == LV_KEY_ESC) { leave_pair(); return true; }
        return false;
    }
    if (!s_in_live) return false;
    if (k == LV_KEY_ESC) { leave_live(); return true; }
    uint8_t mod = 0, usage = 0;
    switch (k) {
    case LV_KEY_UP: usage = HID_UP; break;
    case LV_KEY_DOWN: usage = HID_DOWN; break;
    case LV_KEY_LEFT: usage = HID_LEFT; caret_move(-1); break;
    case LV_KEY_RIGHT: usage = HID_RIGHT; caret_move(1); break;
    default:
        if (k > 0xFF || !hid_from_ascii((uint8_t)k, &mod, &usage)) return true;
        if (k >= 0x20 || k == '\n' || k == '\r' || k == 0x08) echo((uint8_t)k);
    }
    out_tap(mod, usage);
    return true;
}

// ---- building ------------------------------------------------------------------

static lv_obj_t *column(lv_obj_t *parent)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 4, 0);
    lv_obj_set_style_pad_right(c, 4, 0);
    lv_obj_set_style_text_font(c, &lv_font_montserrat_14, 0);
    lv_obj_add_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user = nullptr)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_width(b, LV_PCT(100));
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    lv_obj_add_event_cb(b, [](lv_event_t *e) { lv_obj_scroll_to_view_recursive(lv_event_get_target(e), LV_ANIM_ON); },
                        LV_EVENT_FOCUSED, nullptr);
    return b;
}

// Settings-style row: label on the left, a fixed-width dropdown on the right (#153)
static lv_obj_t *dropdown_row(lv_obj_t *parent, const char *label, const char *opts, int sel, lv_event_cb_t cb)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_t *l = lv_label_create(r);
    lv_label_set_text(l, label);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *dd = lv_dropdown_create(r);
    lv_dropdown_set_options(dd, opts);
    lv_dropdown_set_selected(dd, sel);
    lv_obj_set_width(dd, 150);
    lv_obj_set_style_pad_ver(dd, 5, 0); // 28 px rows: the whole page but Forget fits above the fold
    lv_obj_align(dd, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_text_font(lv_dropdown_get_list(dd), &lv_font_montserrat_14, 0); // the list is not a child
    lv_obj_add_event_cb(dd, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(dd, [](lv_event_t *e) { lv_obj_scroll_to_view_recursive(lv_event_get_target(e), LV_ANIM_ON); },
                        LV_EVENT_FOCUSED, nullptr);
    return dd;
}

static lv_obj_t *s_dongle_row;      // Pair and Sync side by side, dongle target only
static uint32_t s_forget_at = 0;    // Forget needs a second press within FORGET_MS (a stray touch once wiped the pairing)
static const uint32_t FORGET_MS = 4000;

static void build_setup(lv_obj_t *parent)
{
    s_setup = column(parent);
    s_target_dd = dropdown_row(s_setup, "Send to", "Bluetooth\nUSB dongle", s_target, [](lv_event_t *) {
        s_target = (Target)lv_dropdown_get_selected(s_target_dd);
        save_prefs();
        apply_target();
        show(s_setup);
    });
    s_ball_dd = dropdown_row(s_setup, "Trackball", "Mouse\nArrow keys", s_ball, [](lv_event_t *) {
        s_ball = (Ball)lv_dropdown_get_selected(s_ball_dd);
        save_prefs();
    });
    // Mesh relay: chat goes to the dongle's serial port and back, even with this app closed
    s_relay_dd = dropdown_row(s_setup, "Mesh chat", "Off\nTo dongle", companion::enabled(), [](lv_event_t *) {
        companion::set_enabled(lv_dropdown_get_selected(s_relay_dd) == 1);
        if (companion::enabled() && s_target == T_DONGLE) dongle::start();
    });
    s_status = lv_label_create(s_setup);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_status, LV_PCT(100));
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_status, T_DIM, 0);

    // One big touch target: the typing screen itself has touch off, so this is where a thumb goes
    s_start_btn = button(s_setup, LV_SYMBOL_KEYBOARD "   Start typing", [](lv_event_t *) { enter_live(); });
    lv_obj_set_height(s_start_btn, 50);
    lv_obj_set_style_border_width(s_start_btn, 2, 0);
    lv_obj_set_style_text_font(s_start_btn, t_font_mono, 0);

    s_dongle_row = lv_obj_create(s_setup);
    lv_obj_remove_style_all(s_dongle_row);
    lv_obj_set_size(s_dongle_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_dongle_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s_dongle_row, 6, 0);
    s_pair_btn = button(s_dongle_row, LV_SYMBOL_USB "  Pair a dongle", [](lv_event_t *) { enter_pair(); });
    s_sync_btn = button(s_dongle_row, LV_SYMBOL_UPLOAD "  Sync notes", [](lv_event_t *) {
        if (!companion::sync_notes()) ui_toast("Dongle is not online");
    });
    for (lv_obj_t *b : {s_pair_btn, s_sync_btn}) { lv_obj_set_size(b, 0, 32); lv_obj_set_flex_grow(b, 1); }

    s_forget_btn = button(s_setup, LV_SYMBOL_TRASH "  Forget", [](lv_event_t *) {
        uint32_t now = millis();
        if (!s_forget_at || now - s_forget_at > FORGET_MS) { s_forget_at = now; update_status(); return; }
        s_forget_at = 0;
        if (s_target == T_BLUETOOTH) { hid_ble::forget_hosts(); ui_toast("Bluetooth hosts forgotten"); }
        else { dongle::forget(); ui_toast("Dongle forgotten"); }
        update_status();
    });
    lv_obj_set_height(s_forget_btn, 32);
    lv_obj_set_style_text_color(s_forget_btn, T_DIM, 0);
}

static void build_live(lv_obj_t *parent)
{
    s_live = lv_obj_create(parent);
    lv_obj_remove_style_all(s_live);
    lv_obj_set_size(s_live, LV_PCT(100), LV_PCT(100));
    s_live_status = lv_label_create(s_live);
    lv_obj_set_style_text_font(s_live_status, &lv_font_montserrat_12, 0);
    lv_obj_set_width(s_live_status, LV_PCT(100));
    lv_obj_set_y(s_live_status, 6);
    lv_label_set_long_mode(s_live_status, LV_LABEL_LONG_CLIP);
    s_term_box = lv_obj_create(s_live);
    lv_obj_set_size(s_term_box, LV_PCT(100), 160);
    lv_obj_set_y(s_term_box, 22);
    lv_obj_add_event_cb(s_term_box, term_clicked, LV_EVENT_CLICKED, nullptr);
    // Clear empties the T-Deck's copy of the text once it is on the computer; nothing is sent.
    // Esc and the trackball hold leave the screen (#157)
    lv_obj_t *cb = lv_btn_create(s_live);
    lv_obj_set_size(cb, LV_PCT(100), 28);
    lv_obj_align(cb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(cb, &lv_font_montserrat_14, 0);
    lv_obj_t *cl = lv_label_create(cb);
    lv_label_set_text(cl, LV_SYMBOL_TRASH "  Clear");
    lv_obj_center(cl);
    lv_obj_add_event_cb(cb, [](lv_event_t *) {
        s_echo[0] = 0;
        s_echo_len = s_caret = 0;
        echo_redraw();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_style_bg_color(s_term_box, T_BG, 0);
    lv_obj_set_style_border_color(s_term_box, T_DIM, 0);
    lv_obj_set_style_border_width(s_term_box, 1, 0);
    lv_obj_set_style_radius(s_term_box, 2, 0);
    lv_obj_set_style_pad_all(s_term_box, 4, 0);
    s_term = lv_label_create(s_term_box);
    lv_label_set_long_mode(s_term, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_term, LV_PCT(100));
    lv_obj_set_style_text_font(s_term, t_font_mono, 0);
    s_caret_obj = lv_obj_create(s_term);
    lv_obj_remove_style_all(s_caret_obj);
    lv_obj_set_size(s_caret_obj, 2, lv_font_get_line_height(t_font_mono));
    lv_obj_set_style_bg_color(s_caret_obj, T_HEAD, 0);
    lv_obj_set_style_bg_opa(s_caret_obj, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_caret_obj, LV_OBJ_FLAG_CLICKABLE);
    echo_redraw();
}

static void build_pair(lv_obj_t *parent)
{
    s_pairv = column(parent);
    lv_obj_t *t = lv_label_create(s_pairv);
    lv_label_set_text(t, "Pair a dongle");
    s_pair_note = lv_label_create(s_pairv);
    lv_label_set_long_mode(s_pair_note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_pair_note, LV_PCT(100));
    lv_obj_set_style_text_font(s_pair_note, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_pair_note, T_DIM, 0);
    s_pair_list = lv_obj_create(s_pairv);
    lv_obj_remove_style_all(s_pair_list);
    lv_obj_set_size(s_pair_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_pair_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_pair_list, 4, 0);
}

static void refresh_pair_list()
{
    uint8_t macs[4][6];
    size_t n = dongle::candidates(macs, 4);
    if (n == s_pair_count && !memcmp(macs, s_pair_macs, n * 6)) return;
    s_pair_count = n;
    memcpy(s_pair_macs, macs, n * 6);
    lv_obj_clean(s_pair_list);
    for (size_t i = 0; i < n; i++) {
        char text[40];
        snprintf(text, sizeof(text), LV_SYMBOL_USB " Dongle %02x:%02x:%02x", macs[i][3], macs[i][4], macs[i][5]);
        button(s_pair_list, text, [](lv_event_t *e) {
            size_t idx = (size_t)lv_event_get_user_data(e);
            if (idx < s_pair_count && dongle::pair_with(s_pair_macs[idx])) {
                s_pairing = true;
                lv_label_set_text(s_pair_note, "Pairing...");
            } else {
                ui_toast("Couldn't reach that dongle");
            }
        }, (void *)i);
    }
    if (!n) {
        lv_obj_t *l = lv_label_create(s_pair_list);
        lv_label_set_text(l, LV_SYMBOL_REFRESH " Searching...");
    }
    show(s_pairv);
}

// ---- app -----------------------------------------------------------------------

static void open(lv_obj_t *parent)
{
    Preferences p;
    p.begin("kbd", false); // read-write so a first run creates the namespace instead of logging an error
    s_target = (Target)(p.getUChar("target", T_BLUETOOTH) ? T_DONGLE : T_BLUETOOTH);
    s_ball = (Ball)(p.getUChar("ball", B_MOUSE) ? B_ARROWS : B_MOUSE);
    p.end();
    s_in_live = s_in_pair = false;
    build_setup(parent);
    build_live(parent);
    build_pair(parent);
    ui_set_app_key_hook(keys);
    apply_target();
    show(s_setup);
}

static void update_status()
{
    char buf[120];
    if (s_target == T_BLUETOOTH) {
        snprintf(buf, sizeof(buf), hid_ble::connected() ? LV_SYMBOL_BLUETOOTH " Connected to a host."
                 : !hid_ble::running() ? LV_SYMBOL_WARNING " Bluetooth failed to start."
                 : LV_SYMBOL_BLUETOOTH " Waiting: pair \"T-Deck Keyboard\" from your computer or phone.");
    } else if (!dongle::running()) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " ESP-NOW failed to start.");
    } else if (!dongle::paired()) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_USB " No dongle paired yet.");
    } else {
        uint8_t m[6];
        dongle::peer_mac(m);
        snprintf(buf, sizeof(buf), LV_SYMBOL_USB " Dongle %02x:%02x:%02x %s %s", m[3], m[4], m[5],
                 dongle::online() ? "online." : dongle::old_firmware() ? "runs old firmware: reflash it from dongle/."
                 : "offline: is it plugged in?", companion::status());
    }
    lv_label_set_text(s_status, buf);
    if (s_target == T_DONGLE) lv_obj_clear_flag(s_dongle_row, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_dongle_row, LV_OBJ_FLAG_HIDDEN);
    if (s_forget_at && millis() - s_forget_at > FORGET_MS) s_forget_at = 0;
    lv_label_set_text(lv_obj_get_child(s_forget_btn, 0),
                      s_forget_at ? LV_SYMBOL_WARNING "  Press again to forget"
                      : s_target == T_BLUETOOTH ? LV_SYMBOL_TRASH "  Forget Bluetooth hosts" : LV_SYMBOL_TRASH "  Forget this dongle");
    lv_obj_set_style_text_color(s_forget_btn, s_forget_at ? T_WARN : T_DIM, 0);
    lv_label_set_text_fmt(s_live_status, "%s %s   %s",
                          s_target == T_BLUETOOTH ? LV_SYMBOL_BLUETOOTH : LV_SYMBOL_USB,
                          out_ready() ? "sending" : "not connected",
                          s_ball != B_MOUSE ? "hold trackball to stop"
                          : s_scroll ? "scrolling   click = mouse" : "2x = scroll   hold = drag   3 s = stop");
    lv_obj_set_style_text_color(s_live_status, out_ready() ? T_TEXT : T_WARN, 0);
}

static void tick()
{
    dongle::tick();
    static uint32_t last_status = 0, last_batt = 0;
    if (millis() - last_status > 500) {
        last_status = millis();
        bool pair_was_hidden = lv_obj_has_flag(s_dongle_row, LV_OBJ_FLAG_HIDDEN);
        update_status();
        if (!s_in_live && !s_in_pair && pair_was_hidden != lv_obj_has_flag(s_dongle_row, LV_OBJ_FLAG_HIDDEN)) show(s_setup);
        if (s_in_pair) refresh_pair_list();
    }
    if (millis() - last_batt > 60000) {
        last_batt = millis();
        hid_ble::set_battery(power_battery_percent());
    }
    if (s_in_pair && s_pairing) {
        dongle::PairState st = dongle::pair_state();
        if (st == dongle::PAIR_FAILED) {
            s_pairing = false;
            lv_label_set_text(s_pair_note, "Pairing failed. Hold the dongle's BOOT button 2 s and try again.");
            dongle::pair_listen(true);
        } else if (st == dongle::PAIR_DONE) {
            s_pairing = false;
            ui_toast("Dongle paired");
            leave_pair();
        }
    }
    if (s_in_live && s_ball == B_MOUSE) mouse_tick();
    static uint32_t arrow_at = 0;
    if (s_in_live && s_arrows_due && millis() - arrow_at >= 10) {
        arrow_at = millis();
        out_tap(0, s_arrows_due < 0 ? HID_LEFT : HID_RIGHT);
        s_arrows_due += s_arrows_due < 0 ? 1 : -1;
    }
}

static void close()
{
    input_set_touch(true);
    if (s_in_live) leave_live();
    input_set_trackball_mode(TB_FOCUS);
    dongle::pair_listen(false);
    hid_ble::stop();
    if (!companion::enabled()) dongle::stop();
}

const App app_keyboard = {"Keyboard", LV_SYMBOL_KEYBOARD, open, close, tick, nullptr};
