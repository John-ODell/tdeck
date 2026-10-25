#include "apps.h"
#include "ui/theme.h"
#include <Arduino.h>
#include <time.h>
#include "core/messages.h"
#include "core/nodedb.h"
#include "mesh/mesh.h"
#include "mesh/protocol.h"
#include "ui/util.h"

static lv_obj_t *s_root = nullptr, *s_list = nullptr, *s_ta = nullptr;
static bool s_in_thread = false;
static uint32_t s_peer = mt::BROADCAST;
static uint32_t s_seen_version = 0;
static uint32_t s_pending_peer = 0; // set by chat_open_peer before the app opens

static void show_list();
static void show_thread(uint32_t peer);

static const char *status_glyph(const Message &m)
{
    switch (m.status) {
    case MSG_QUEUED: return "...";
    case MSG_SENT: return LV_SYMBOL_OK;
    case MSG_RELAYED: return LV_SYMBOL_OK LV_SYMBOL_OK;
    case MSG_ACKED: return LV_SYMBOL_OK LV_SYMBOL_OK;
    case MSG_FAILED: return LV_SYMBOL_CLOSE;
    default: return "";
    }
}

static void conv_clicked(lv_event_t *e)
{
    show_thread((uint32_t)(uintptr_t)lv_event_get_user_data(e));
}

static void show_list()
{
    s_in_thread = false;
    ui_set_app_key_hook(nullptr);
    lv_obj_clean(s_root);
    lv_group_remove_all_objs(ui_group());
    s_list = lv_list_create(s_root);
    lv_obj_set_size(s_list, LV_PCT(100), LV_PCT(100));

    // Conversations: channel first, then DM peers by latest message
    uint32_t peers[32];
    size_t np = 0;
    uint32_t me = mesh::our_num();
    for (size_t i = messages::count(); i-- > 0 && np < 32;) {
        uint32_t p = messages::peer_of(*messages::at(i), me);
        if (p == mt::BROADCAST) continue;
        bool dup = false;
        for (size_t j = 0; j < np; j++) dup |= peers[j] == p;
        if (!dup) peers[np++] = p;
    }

    char text[80];
    size_t unread = messages::unread_for(mt::BROADCAST, me);
    snprintf(text, sizeof(text), "#LongFast (channel)%s", unread ? "  " LV_SYMBOL_BELL : "");
    lv_obj_t *b = ui_list_btn(s_list, LV_SYMBOL_LIST, text);
    lv_obj_add_event_cb(b, conv_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)mt::BROADCAST);

    for (size_t i = 0; i < np; i++) {
        char name[44];
        nodedb::display_name(peers[i], name, sizeof(name));
        unread = messages::unread_for(peers[i], me);
        if (unread) snprintf(text, sizeof(text), "%s  (%u new)", name, (unsigned)unread);
        else strlcpy(text, name, sizeof(text));
        b = ui_list_btn(s_list, LV_SYMBOL_ENVELOPE, text);
        lv_obj_add_event_cb(b, conv_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)peers[i]);
    }
    lv_list_add_text(s_list, "Start a DM from the Nodes app");
    s_seen_version = messages::version();
}

static void add_bubble(lv_obj_t *parent, const Message &m, bool mine)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);

    lv_obj_t *bubble = lv_obj_create(row);
    lv_obj_set_style_radius(bubble, 2, 0);
    lv_obj_set_style_border_width(bubble, 1, 0);
    lv_obj_set_style_border_color(bubble, mine ? T_DIM : T_SURFACE_HI, 0);
    lv_obj_set_style_pad_all(bubble, 5, 0);
    lv_obj_set_style_bg_color(bubble, mine ? T_SURFACE_HI : T_SURFACE, 0);
    lv_obj_set_size(bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(bubble, 250, 0);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(bubble, mine ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);

    char meta[80], when[12] = "";
    if (m.time) {
        struct tm t;
        time_t tt = m.time;
        localtime_r(&tt, &t);
        strftime(when, sizeof(when), "%H:%M", &t);
    }
    if (mine) {
        snprintf(meta, sizeof(meta), "%s %s%s", when, m.pki ? LV_SYMBOL_EYE_CLOSE " " : "", status_glyph(m));
    } else {
        char name[44];
        nodedb::display_name(m.from, name, sizeof(name));
        snprintf(meta, sizeof(meta), "%s  %s%s", name, when, m.pki ? "  " LV_SYMBOL_EYE_CLOSE : "");
    }
    lv_obj_t *ml = lv_label_create(bubble);
    lv_label_set_text(ml, meta);
    lv_obj_set_style_text_font(ml, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ml, T_DIM, 0);

    // Fixed width from the measured text: a wrapping SIZE_CONTENT label inside a
    // SIZE_CONTENT bubble can wrap at a stale width on the first layout pass
    lv_obj_t *tl = lv_label_create(bubble);
    lv_label_set_long_mode(tl, LV_LABEL_LONG_WRAP);
    const lv_font_t *font = lv_obj_get_style_text_font(tl, LV_PART_MAIN);
    lv_point_t sz;
    lv_txt_get_size(&sz, m.text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_width(tl, LV_MIN(sz.x + 1, 240));
    lv_label_set_text(tl, m.text);
}

static void fill_thread()
{
    lv_obj_clean(s_list);
    uint32_t me = mesh::our_num();
    size_t shown = 0;
    // Newest 60 messages of this conversation
    size_t first = messages::count();
    for (size_t i = messages::count(); i-- > 0 && shown < 60;) {
        if (messages::peer_of(*messages::at(i), me) == s_peer) { first = i; shown++; }
    }
    for (size_t i = first; i < messages::count(); i++) {
        Message *m = messages::at(i);
        if (messages::peer_of(*m, me) == s_peer) add_bubble(s_list, *m, m->from == me);
    }
    if (!shown) {
        lv_obj_t *l = lv_label_create(s_list);
        lv_label_set_text(l, s_peer == mt::BROADCAST ? "No channel messages yet." : "No messages yet.");
    }
    lv_obj_update_layout(s_list);
    lv_obj_scroll_to_y(s_list, LV_COORD_MAX, LV_ANIM_OFF);
    messages::mark_read(s_peer, me);
    s_seen_version = messages::version();
}

static void ta_ready(lv_event_t *)
{
    const char *txt = lv_textarea_get_text(s_ta);
    if (!txt[0]) return;
    if (!mesh::send_text(s_peer, txt)) ui_toast(mesh::radio_ok() ? "Send failed (too long?)" : "No radio");
    lv_textarea_set_text(s_ta, "");
    fill_thread();
}

static bool thread_keys(uint32_t key)
{
    if (key == LV_KEY_PREV) { lv_obj_scroll_by_bounded(s_list, 0, 40, LV_ANIM_ON); return true; }
    if (key == LV_KEY_NEXT) { lv_obj_scroll_by_bounded(s_list, 0, -40, LV_ANIM_ON); return true; }
    return false;
}

static void show_thread(uint32_t peer)
{
    s_in_thread = true;
    s_peer = peer;
    lv_obj_clean(s_root);
    lv_group_remove_all_objs(ui_group());

    char name[44];
    lv_obj_t *title = lv_label_create(s_root);
    if (peer == mt::BROADCAST) lv_label_set_text(title, "#LongFast  (everyone in range)");
    else {
        nodedb::display_name(peer, name, sizeof(name));
        Node *n = nodedb::find(peer);
        lv_label_set_text_fmt(title, "DM %s%s", name, n && n->has_key ? "  " LV_SYMBOL_EYE_CLOSE : "  (no key yet)");
    }
    lv_obj_set_style_text_color(title, T_ACCENT, 0);

    s_list = lv_obj_create(s_root);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_list, 2, 0);
    lv_obj_set_style_pad_row(s_list, 4, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(s_list, LV_PCT(100), 150);
    lv_obj_set_y(s_list, 18);

    s_ta = lv_textarea_create(s_root);
    lv_textarea_set_one_line(s_ta, true);
    lv_textarea_set_max_length(s_ta, 200);
    lv_textarea_set_placeholder_text(s_ta, "Type, Enter to send");
    lv_obj_set_size(s_ta, LV_PCT(100), 38);
    lv_obj_align(s_ta, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(s_ta, ta_ready, LV_EVENT_READY, nullptr);
    lv_group_add_obj(ui_group(), s_ta);
    lv_group_focus_obj(s_ta);

    ui_set_app_key_hook(thread_keys);
    fill_thread();
}

static void open(lv_obj_t *parent)
{
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    if (s_pending_peer) {
        uint32_t p = s_pending_peer;
        s_pending_peer = 0;
        show_thread(p);
    } else {
        show_list();
    }
}

static void tick()
{
    if (messages::version() == s_seen_version) return;
    if (s_in_thread) {
        // Keep a draft intact while refreshing
        fill_thread();
    } else {
        show_list();
    }
}

static bool on_back()
{
    if (s_in_thread) { show_list(); return true; }
    return false;
}

static void close() { s_root = s_list = s_ta = nullptr; s_in_thread = false; }

void chat_open_peer(uint32_t peer)
{
    s_pending_peer = peer;
    ui_open_app(&app_chat);
}

bool chat_viewing(uint32_t peer)
{
    return ui_current_app() == &app_chat && s_in_thread && s_peer == peer;
}

const App app_chat = {"Chat", LV_SYMBOL_ENVELOPE, open, close, tick, on_back};
