// Offline slippy map. Tiles: 256 px PNG at /sd/map/{z}/{x}/{y}.png or
// /sd/maps/<style>/{z}/{x}/{y}.png (Meshtastic MUI layout). Without tiles it
// draws a distance grid so node positions still make sense.
#include "apps.h"
#include "ui/theme.h"
#include <Arduino.h>
#include <math.h>
#include <sys/stat.h>
#include <dirent.h>
#include "core/nodedb.h"
#include "core/storage.h"
#include "mesh/mesh.h"
#include "ui/util.h"
#include <vector>
#include <SD.h>
#include "link/companion.h"
#include "link/link.h"

static lv_obj_t *s_view = nullptr, *s_info = nullptr;
static double s_lat = 39.8, s_lon = -98.6;
static int s_zoom = 4;
static bool s_have_center = false;
static uint32_t s_center_node = 0;
static char s_tile_root[48] = "";   // e.g. "/map" (relative to /sd)
static uint32_t s_seen_version = 0;
static const int VIEW_H = 220 - 16;

static double world_px(int z) { return 256.0 * (double)(1u << z); }
static double lon_to_x(double lon, int z) { return (lon + 180.0) / 360.0 * world_px(z); }
static double lat_to_y(double lat, int z)
{
    double r = lat * M_PI / 180.0;
    return (1.0 - log(tan(r) + 1.0 / cos(r)) / M_PI) / 2.0 * world_px(z);
}
static double x_to_lon(double x, int z) { return x / world_px(z) * 360.0 - 180.0; }
static double y_to_lat(double y, int z)
{
    double n = M_PI - 2.0 * M_PI * y / world_px(z);
    return 180.0 / M_PI * atan(0.5 * (exp(n) - exp(-n)));
}

static void find_tile_root()
{
    s_tile_root[0] = 0;
    if (!sd_mount()) return;
    struct stat st;
    if (stat("/sd/map", &st) == 0 && S_ISDIR(st.st_mode)) { strcpy(s_tile_root, "/map"); return; }
    DIR *d = opendir("/sd/maps");
    if (!d) return;
    while (struct dirent *e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        snprintf(s_tile_root, sizeof(s_tile_root), "/maps/%s", e->d_name);
        break;
    }
    closedir(d);
}

static void add_marker(double x0, double y0, int32_t lat_i, int32_t lon_i, const char *label, lv_color_t color)
{
    double x = lon_to_x(lon_i * 1e-7, s_zoom) - x0;
    double y = lat_to_y(lat_i * 1e-7, s_zoom) - y0;
    if (x < -20 || x > 340 || y < -20 || y > VIEW_H + 20) return;
    lv_obj_t *dot = lv_obj_create(s_view);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, color, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(dot, lv_color_white(), 0);
    lv_obj_set_style_border_width(dot, 2, 0);
    lv_obj_set_pos(dot, (lv_coord_t)x - 5, (lv_coord_t)y - 5);
    lv_obj_t *l = lv_label_create(s_view);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_style_bg_color(l, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_50, 0);
    lv_obj_set_pos(l, (lv_coord_t)x + 7, (lv_coord_t)y - 8);
}

static void render()
{
    lv_obj_clean(s_view);
    double cx = lon_to_x(s_lon, s_zoom), cy = lat_to_y(s_lat, s_zoom);
    double x0 = cx - 160, y0 = cy - VIEW_H / 2.0;
    int tiles = 0;

    if (s_tile_root[0]) {
        int n = 1 << s_zoom;
        for (int ty = (int)floor(y0 / 256); ty <= (int)floor((y0 + VIEW_H) / 256); ty++) {
            for (int tx = (int)floor(x0 / 256); tx <= (int)floor((x0 + 320) / 256); tx++) {
                if (ty < 0 || ty >= n) continue;
                int wx = ((tx % n) + n) % n;
                char path[96];
                snprintf(path, sizeof(path), "/sd%s/%d/%d/%d.png", s_tile_root, s_zoom, wx, ty);
                struct stat st;
                if (stat(path, &st) != 0) continue;
                lv_obj_t *img = lv_img_create(s_view);
                char src[100];
                snprintf(src, sizeof(src), "S:%s/%d/%d/%d.png", s_tile_root, s_zoom, wx, ty);
                lv_img_set_src(img, src);
                lv_obj_set_pos(img, (lv_coord_t)(tx * 256 - x0), (lv_coord_t)(ty * 256 - y0));
                tiles++;
            }
        }
    }

    // Grid every "nice" distance so scale is readable without tiles
    double m_per_px = 156543.03392 * cos(s_lat * M_PI / 180.0) / (1u << s_zoom);
    static const double steps[] = {50, 100, 250, 500, 1000, 2000, 5000, 10000, 25000, 50000, 100000, 250000, 500000, 1e6};
    double step = steps[0];
    for (double st : steps) { step = st; if (st / m_per_px >= 60) break; }
    if (!tiles) {
        int px = (int)(step / m_per_px);
        for (int gx = (int)(-fmod(x0, px)); gx < 320; gx += px) {
            lv_obj_t *ln = lv_obj_create(s_view);
            lv_obj_remove_style_all(ln);
            lv_obj_set_style_bg_color(ln, T_SURFACE_HI, 0);
            lv_obj_set_style_bg_opa(ln, LV_OPA_COVER, 0);
            lv_obj_set_size(ln, 1, VIEW_H);
            lv_obj_set_pos(ln, gx, 0);
        }
        for (int gy = (int)(-fmod(y0, px)); gy < VIEW_H; gy += px) {
            lv_obj_t *ln = lv_obj_create(s_view);
            lv_obj_remove_style_all(ln);
            lv_obj_set_style_bg_color(ln, T_SURFACE_HI, 0);
            lv_obj_set_style_bg_opa(ln, LV_OPA_COVER, 0);
            lv_obj_set_size(ln, 320, 1);
            lv_obj_set_pos(ln, 0, gy);
        }
    }

    size_t shown = 0;
    for (size_t i = 0; i < nodedb::count(); i++) {
        Node *n = nodedb::at(i);
        if (!n->has_pos) continue;
        add_marker(x0, y0, n->lat_i, n->lon_i, n->short_name,
                   n->num == s_center_node ? T_WARN : T_TEXT);
        shown++;
    }
    int32_t mlat, mlon;
    if (mesh::own_position(&mlat, &mlon)) add_marker(x0, y0, mlat, mlon, "me", T_HEAD);

    char scale[16];
    fmt_distance(step, scale, sizeof(scale));
    lv_label_set_text_fmt(s_info, "z%d  %.4f,%.4f  grid %s  %u nodes  %s", s_zoom, s_lat, s_lon, scale,
                          (unsigned)shown, s_tile_root[0] ? (tiles ? "" : "no tiles here") : "no SD tiles");
    s_seen_version = nodedb::version();
}

static void pan(double dx, double dy)
{
    double cx = lon_to_x(s_lon, s_zoom) + dx, cy = lat_to_y(s_lat, s_zoom) + dy;
    s_lon = x_to_lon(cx, s_zoom);
    s_lat = y_to_lat(cy, s_zoom);
    if (s_lat > 85) s_lat = 85;
    if (s_lat < -85) s_lat = -85;
    render();
}

static bool center_on_me()
{
    int32_t lat, lon;
    if (!mesh::own_position(&lat, &lon)) return false;
    s_lat = lat * 1e-7;
    s_lon = lon * 1e-7;
    return true;
}

// ---- tiles from the dongle's card (s) -------------------------------------------------
// Walks map/ on the GEEK's card to any depth and pulls every PNG the SD card lacks or
// holds at another size (#136).

extern const App app_map;
struct Pull { String remote, local; };
static std::vector<String> s_sync_dirs;
static std::vector<Pull> s_sync_pulls;
static int s_sync_got = 0;
static bool s_syncing = false;

static void sync_step();

static void sync_finish(const char *msg)
{
    s_syncing = false;
    lv_img_cache_invalidate_src(NULL); // a replaced tile must not keep showing its old decode
    ui_toast(msg);
    if (ui_current_app() == &app_map) { find_tile_root(); render(); }
}

static void pulled(bool ok)
{
    if (ok) s_sync_got++;
    else { char m[64]; snprintf(m, sizeof(m), "pull failed: %s", companion::status()); sync_finish(m); return; }
    if (s_sync_got % 20 == 0) { char m[40]; snprintf(m, sizeof(m), "%d tiles, %d to go", s_sync_got, (int)s_sync_pulls.size()); ui_toast(m); }
    sync_step();
}

static void listed(bool ok, const companion::FsEntry *e, size_t n)
{
    if (!ok) { sync_finish("no map/ folder on the card"); return; }
    String dir = s_sync_dirs.front();
    s_sync_dirs.erase(s_sync_dirs.begin());
    for (size_t i = 0; i < n; i++) {
        String name = e[i].name;
        if (e[i].dir) { s_sync_dirs.push_back(dir + "/" + name); continue; }
        if (!name.endsWith(".png") || name.startsWith("._")) continue; // skip macOS resource forks
        String local = "/" + dir + "/" + name;
        File f = SD.open(local, "r");
        bool same = f && f.size() == e[i].size; // a tile that changed on the card is pulled again
        if (f) f.close();
        if (!same) s_sync_pulls.push_back({dir + "/" + name, local});
    }
    sync_step();
}

static void sync_step()
{
    if (!s_sync_dirs.empty()) {
        if (!companion::fs_list(s_sync_dirs.front().c_str(), listed)) sync_finish("dongle is busy");
        return;
    }
    if (!s_sync_pulls.empty()) {
        Pull p = s_sync_pulls.front();
        s_sync_pulls.erase(s_sync_pulls.begin());
        if (!companion::fs_get(p.remote.c_str(), p.local.c_str(), pulled)) sync_finish("dongle is busy");
        return;
    }
    char m[48];
    snprintf(m, sizeof(m), s_sync_got ? "pulled %d tiles" : "no new tiles on the card", s_sync_got);
    sync_finish(m);
}

static void sync_start()
{
    if (!sd_ok()) { ui_toast("Tiles need an SD card"); return; }
    if (!dongle::online()) { ui_toast("Dongle is offline"); return; }
    if (s_syncing || companion::busy()) { ui_toast("Already pulling"); return; }
    s_syncing = true;
    s_sync_got = 0;
    s_sync_dirs = {"map"};
    s_sync_pulls.clear();
    ui_toast("Pulling tiles from the card");
    sync_step();
}

static bool keys(uint32_t k)
{
    switch (k) {
    case LV_KEY_PREV: pan(0, -64); return true;
    case LV_KEY_NEXT: pan(0, 64); return true;
    case LV_KEY_LEFT: pan(-80, 0); return true;
    case LV_KEY_RIGHT: pan(80, 0); return true;
    case '+': case '=': case 'i': case LV_KEY_ENTER: if (s_zoom < 18) s_zoom++; render(); return true;
    case '-': case '_': case 'o': if (s_zoom > 1) s_zoom--; render(); return true;
    case 'c': if (center_on_me()) render(); else ui_toast("No GPS fix yet"); return true;
    case 's': sync_start(); return true;
    case 'n': {
        // Cycle through nodes with positions
        static size_t idx = 0;
        for (size_t tries = 0; tries < nodedb::count(); tries++) {
            Node *n = nodedb::at(idx++ % nodedb::count());
            if (n && n->has_pos) {
                s_center_node = n->num;
                s_lat = n->lat_i * 1e-7;
                s_lon = n->lon_i * 1e-7;
                render();
                ui_toast(n->has_user ? n->long_name : n->short_name);
                break;
            }
        }
        return true;
    }
    }
    return false;
}

static void open(lv_obj_t *parent)
{
    find_tile_root();
    if (!s_have_center) {
        s_have_center = true;
        if (center_on_me()) s_zoom = 12;
        else {
            for (size_t i = 0; i < nodedb::count(); i++) {
                Node *n = nodedb::at(i);
                if (n->has_pos) { s_lat = n->lat_i * 1e-7; s_lon = n->lon_i * 1e-7; s_zoom = 10; break; }
            }
        }
    }
    lv_obj_set_style_pad_all(parent, 0, 0);
    s_view = lv_obj_create(parent);
    lv_obj_remove_style_all(s_view);
    lv_obj_set_size(s_view, 320, VIEW_H);
    lv_obj_set_style_bg_color(s_view, T_BG, 0);
    lv_obj_set_style_bg_opa(s_view, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_view, LV_OBJ_FLAG_SCROLLABLE);
    s_info = lv_label_create(parent);
    lv_obj_set_style_text_font(s_info, &lv_font_montserrat_12, 0);
    lv_obj_align(s_info, LV_ALIGN_BOTTOM_LEFT, 4, 0);
    ui_set_app_key_hook(keys);
    render();
    ui_toast("Trackball pans, click/+ zoom in, - out, c = me, n = next node, s = pull tiles");
}

static void tick()
{
    static uint32_t last = 0;
    if (nodedb::version() != s_seen_version && millis() - last > 10000) { last = millis(); render(); }
}

static void close()
{
    s_view = s_info = nullptr;
}

void map_center_on(uint32_t node_num)
{
    Node *n = nodedb::find(node_num);
    if (n && n->has_pos) {
        s_have_center = true;
        s_center_node = node_num;
        s_lat = n->lat_i * 1e-7;
        s_lon = n->lon_i * 1e-7;
        if (s_zoom < 12) s_zoom = 12;
    }
    ui_open_app(&app_map);
}

const App app_map = {"Map", LV_SYMBOL_IMAGE, open, close, tick, nullptr};
