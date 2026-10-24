#include "apps.h"
#include <Arduino.h>
#include "core/nodedb.h"
#include "mesh/mesh.h"
#include "ui/util.h"

static lv_obj_t *s_root = nullptr, *s_list = nullptr;
static uint32_t s_seen_version = 0, s_last_refresh = 0;
static bool s_in_detail = false;
static uint32_t s_selected = 0;

static void show_list();

static void action(lv_event_t *e)
{
    int a = (int)(intptr_t)lv_event_get_user_data(e);
    switch (a) {
    case 0: chat_open_peer(s_selected); return;
    case 1: mesh::send_position(s_selected); ui_toast("Position requested"); break;
    case 2: mesh::send_nodeinfo(s_selected, true); ui_toast("Node info requested"); break;
    case 3: map_center_on(s_selected); return;
    }
}

static void show_detail(uint32_t num)
{
    Node *n = nodedb::find(num);
    if (!n) return;
    s_in_detail = true;
    s_selected = num;
    lv_obj_clean(s_root);
    lv_group_remove_all_objs(ui_group());

    char age[12], dist[24] = "-", line[320];
    fmt_age(n->last_heard, age, sizeof(age));
    int32_t mlat, mlon;
    if (n->has_pos && mesh::own_position(&mlat, &mlon)) {
        double d = geo_distance_m(mlat, mlon, n->lat_i, n->lon_i);
        char tmp[16];
        snprintf(dist, sizeof(dist), "%s %s", fmt_distance(d, tmp, sizeof(tmp)),
                 compass(geo_bearing_deg(mlat, mlon, n->lat_i, n->lon_i)));
    }
    snprintf(line, sizeof(line),
             "%s (%s)\n!%08x   hw %u   %s\n"
             "Heard %s ago   %s\n"
             "SNR %.1f dB   RSSI %d dBm\n"
             "Battery %s   %.2f V\n"
             "Position %s   Distance %s",
             n->has_user ? n->long_name : "(unknown)", n->short_name, (unsigned)n->num, n->hw_model,
             n->has_key ? "PKI key " LV_SYMBOL_OK : "no key",
             age, n->hops_away == 0xFF ? "hops ?" : n->hops_away == 0 ? "direct" : (String(n->hops_away) + " hops").c_str(),
             n->snr, n->rssi,
             n->battery == 0 ? "?" : n->battery > 100 ? "USB" : (String(n->battery) + "%").c_str(), n->voltage,
             n->has_pos ? "yes" : "no", dist);
    lv_obj_t *l = lv_label_create(s_root);
    lv_label_set_text(l, line);

    lv_obj_t *row = lv_obj_create(s_root);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 40);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const char *labels[] = {LV_SYMBOL_ENVELOPE " DM", LV_SYMBOL_GPS " Pos?", LV_SYMBOL_REFRESH " Info?", LV_SYMBOL_IMAGE " Map"};
    for (int i = 0; i < 4; i++) {
        if (i == 3 && !n->has_pos) continue;
        lv_obj_t *b = lv_btn_create(row);
        lv_obj_t *bl = lv_label_create(b);
        lv_label_set_text(bl, labels[i]);
        lv_obj_add_event_cb(b, action, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_group_add_obj(ui_group(), b);
    }
}

static void node_clicked(lv_event_t *e)
{
    show_detail((uint32_t)(uintptr_t)lv_event_get_user_data(e));
}

static void show_list()
{
    s_in_detail = false;
    lv_obj_clean(s_root);
    lv_group_remove_all_objs(ui_group());
    s_list = lv_list_create(s_root);
    lv_obj_set_size(s_list, LV_PCT(100), LV_PCT(100));

    nodedb::sort_by_last_heard();
    int32_t mlat, mlon;
    bool have_me = mesh::own_position(&mlat, &mlon);
    char text[96], age[12];
    ui_list_text(s_list, (String(nodedb::count()) + " nodes heard").c_str());
    for (size_t i = 0; i < nodedb::count() && i < 80; i++) {
        Node *n = nodedb::at(i);
        fmt_age(n->last_heard, age, sizeof(age));
        char extra[32] = "";
        if (have_me && n->has_pos) {
            char d[16];
            fmt_distance(geo_distance_m(mlat, mlon, n->lat_i, n->lon_i), d, sizeof(d));
            snprintf(extra, sizeof(extra), "  %s", d);
        }
        const char *hops = n->hops_away == 0 ? "direct" : n->hops_away == 0xFF ? "" : "hop";
        snprintf(text, sizeof(text), "%-4s %s\n%s  %s%s%s", n->short_name,
                 n->has_user ? n->long_name : "", age, hops,
                 n->hops_away > 0 && n->hops_away < 8 ? (String(" x") + n->hops_away).c_str() : "", extra);
        lv_obj_t *b = ui_list_btn(s_list, n->has_key ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_WIFI, text);
        lv_obj_add_event_cb(b, node_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)n->num);
    }
    s_seen_version = nodedb::version();
    s_last_refresh = millis();
}

static void open(lv_obj_t *parent)
{
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    show_list();
}

static void tick()
{
    // Rebuilding steals focus, so refresh the list at most every 15 s
    if (!s_in_detail && nodedb::version() != s_seen_version && millis() - s_last_refresh > 15000) show_list();
}

static bool on_back()
{
    if (s_in_detail) { show_list(); return true; }
    return false;
}

static void close() { s_root = s_list = nullptr; s_in_detail = false; }

const App app_nodes = {"Nodes", LV_SYMBOL_LIST, open, close, tick, on_back};
