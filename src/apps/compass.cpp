// Compass: a heading-up rose from the GPS course (the board has no magnetometer, so there
// is no heading while standing still), speed and altitude, a trip that keeps counting with
// the app closed, and a waypoint to find the way back to.
#include "apps.h"
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include "hal/gps.h"
#include "ui/theme.h"
#include "ui/util.h"

// ---- trip, runs from an LVGL timer whether or not the app is open -------------------

static double trip_m = 0, trip_max_kmh = 0;
static uint32_t trip_moving_ms = 0, trip_started = 0;
static double last_lat = 0, last_lon = 0;
static bool have_last = false;
static double heading = -1;              // last good course, -1 = none yet
static bool way_set = false;
static double way_lat = 0, way_lon = 0;

static const double MOVING_KMH = 2.0;    // below this the course is noise

static int32_t e7(double deg) { return (int32_t)llround(deg * 1e7); }

static void trip_tick(lv_timer_t *)
{
    const GpsState &g = gps_state();
    if (!g.fix || g.hdop > 6) return;
    if (!trip_started) trip_started = millis();
    bool moving = g.speed_kmh >= MOVING_KMH;
    if (moving) {
        heading = g.course_deg;
        trip_moving_ms += 1000;
        if (g.speed_kmh > trip_max_kmh) trip_max_kmh = g.speed_kmh;
        if (have_last) {
            double d = geo_distance_m(e7(last_lat), e7(last_lon), e7(g.lat), e7(g.lon));
            if (d < 200) trip_m += d; // a jump that big in a second is a bad fix
        }
    }
    last_lat = g.lat;
    last_lon = g.lon;
    have_last = true;
}

void trip_init()
{
    Preferences p;
    p.begin("trip", true);
    way_set = p.isKey("wlat");
    if (way_set) { way_lat = p.getDouble("wlat"); way_lon = p.getDouble("wlon"); }
    p.end();
    lv_timer_create(trip_tick, 1000, nullptr);
}

static void save_waypoint()
{
    Preferences p;
    p.begin("trip", false);
    if (way_set) { p.putDouble("wlat", way_lat); p.putDouble("wlon", way_lon); }
    else p.clear();
    p.end();
}

// ---- the screen ------------------------------------------------------------------------

static lv_obj_t *s_root = nullptr, *s_ring, *s_needle, *s_card[4], *s_mark, *s_hdg, *s_info, *s_hint;
static lv_point_t s_needle_pts[3];
static const int CX = 86, CY = 100, R = 72;  // rose centre and radius inside s_root

static void place(lv_obj_t *o, double deg_from_up, int r)
{
    // deg_from_up is clockwise from the top of the screen
    double a = deg_from_up * M_PI / 180.0;
    int x = CX + (int)lround(r * sin(a)), y = CY - (int)lround(r * cos(a));
    lv_obj_update_layout(o);
    lv_obj_set_pos(o, x - lv_obj_get_width(o) / 2, y - lv_obj_get_height(o) / 2);
}

static void refresh()
{
    if (!s_root) return;
    const GpsState &g = gps_state();
    bool moving = g.fix && g.speed_kmh >= MOVING_KMH;
    double h = heading < 0 ? 0 : heading;

    // The rose turns so the heading points up; without a heading north is up
    static const char *names[4] = {"N", "E", "S", "W"};
    for (int i = 0; i < 4; i++) {
        lv_label_set_text(s_card[i], names[i]);
        place(s_card[i], i * 90.0 - h, R - 14);
    }
    s_needle_pts[0] = {(lv_coord_t)(CX - 7), (lv_coord_t)(CY + 10)};
    s_needle_pts[1] = {(lv_coord_t)CX, (lv_coord_t)(CY - R + 24)};
    s_needle_pts[2] = {(lv_coord_t)(CX + 7), (lv_coord_t)(CY + 10)};
    lv_line_set_points(s_needle, s_needle_pts, 3);
    lv_obj_set_style_line_color(s_needle, moving ? T_ACCENT : T_DIM, 0);

    char hdg[32];
    if (heading < 0) strcpy(hdg, "no heading yet");
    else snprintf(hdg, sizeof(hdg), "%s %.0f%s", compass(heading), heading, moving ? "" : "  (stopped)");
    lv_label_set_text(s_hdg, hdg);

    char spd[24], alt[24], dist[24], mov[24], mx[24], way[64] = "no waypoint  (m = mark here)";
    snprintf(spd, sizeof(spd), g.fix ? "%.1f mph" : "no fix", g.speed_kmh * 0.621371);
    snprintf(alt, sizeof(alt), g.fix ? "%.0f ft" : "-", g.alt_m * 3.28084);
    fmt_distance(trip_m, dist, sizeof(dist));
    uint32_t s = trip_moving_ms / 1000;
    snprintf(mov, sizeof(mov), "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
    snprintf(mx, sizeof(mx), "%.1f mph", trip_max_kmh * 0.621371);
    if (way_set && g.fix) {
        double d = geo_distance_m(e7(g.lat), e7(g.lon), e7(way_lat), e7(way_lon));
        double b = geo_bearing_deg(e7(g.lat), e7(g.lon), e7(way_lat), e7(way_lon));
        char dd[24];
        snprintf(way, sizeof(way), "%s  %s %.0f", fmt_distance(d, dd, sizeof(dd)), compass(b), b);
        place(s_mark, b - h, R - 4);
        lv_obj_clear_flag(s_mark, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (way_set) strcpy(way, "waypoint set, waiting for a fix");
        lv_obj_add_flag(s_mark, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text_fmt(s_info,
        "Speed     %s\nAltitude  %s\n\nTrip      %s\nMoving    %s\nMax       %s\n\nWaypoint  %s",
        spd, alt, dist, mov, mx, way);
}

static bool keys(uint32_t k)
{
    const GpsState &g = gps_state();
    if (k == 'm') {
        if (!g.fix) { ui_toast("No fix to mark"); return true; }
        way_set = true;
        way_lat = g.lat;
        way_lon = g.lon;
        save_waypoint();
        ui_toast("Waypoint set here");
    } else if (k == 'c') {
        way_set = false;
        save_waypoint();
        ui_toast("Waypoint cleared");
    } else if (k == 'r') {
        trip_m = trip_max_kmh = 0;
        trip_moving_ms = 0;
        trip_started = 0;
        ui_toast("Trip reset");
    } else return false;
    refresh();
    return true;
}

static lv_obj_t *small(lv_obj_t *parent, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

static void open(lv_obj_t *parent)
{
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    s_ring = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_ring);
    lv_obj_set_size(s_ring, 2 * R, 2 * R);
    lv_obj_set_pos(s_ring, CX - R, CY - R);
    lv_obj_set_style_radius(s_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_ring, 2, 0);
    lv_obj_set_style_border_color(s_ring, T_DIM, 0);

    s_needle = lv_line_create(s_root);
    lv_obj_set_style_line_width(s_needle, 3, 0);
    lv_obj_set_style_line_rounded(s_needle, true, 0);

    for (int i = 0; i < 4; i++) s_card[i] = small(s_root, i == 0 ? T_ACCENT : T_DIM);
    s_mark = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_mark);
    lv_obj_set_size(s_mark, 10, 10);
    lv_obj_set_style_radius(s_mark, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_mark, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_mark, T_DANGER_HI, 0);

    s_hdg = small(s_root, T_ACCENT);
    lv_obj_set_style_text_font(s_hdg, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(s_hdg, 2, CY + R + 4);

    s_info = small(s_root, T_DIM);
    lv_obj_set_style_text_font(s_info, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(s_info, CX + R + 10, 6);
    lv_obj_set_width(s_info, 320 - (CX + R + 14));
    lv_label_set_long_mode(s_info, LV_LABEL_LONG_WRAP);

    s_hint = small(s_root, T_DIM);
    lv_obj_set_style_text_font(s_hint, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_hint, "m = mark   c = clear   r = reset trip");
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_RIGHT, -2, 0);

    ui_set_app_key_hook(keys);
    refresh();
}

static void tick()
{
    static uint32_t last = 0;
    if (millis() - last > 1000) { last = millis(); refresh(); }
}

static void close() { s_root = nullptr; }

const App app_compass = {"Compass", LV_SYMBOL_GPS, open, close, tick, nullptr};
