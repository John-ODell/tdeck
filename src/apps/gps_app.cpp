#include "apps.h"
#include <Arduino.h>
#include "hal/gps.h"
#include "core/settings.h"
#include "mesh/mesh.h"

static lv_obj_t *s_label, *s_share;

static void refresh()
{
    const GpsState &g = gps_state();
    char pos[64] = "searching...";
    if (g.fix) snprintf(pos, sizeof(pos), "%.6f, %.6f", g.lat, g.lon);
    lv_label_set_text_fmt(s_label,
        "Module   %s @ %lu baud%s\n"
        "Fix      %s   sats %u   HDOP %.1f\n"
        "Position %s\n"
        "Altitude %.0f ft   Speed %.1f mph\n"
        "Course   %.0f deg\n"
        "UTC      %04d-%02d-%02d %02d:%02d:%02d\n"
        "NMEA     %lu chars",
        g.model, (unsigned long)g.baud, g.present ? "" : "  (no data yet)",
        g.fix ? "3D/2D" : "none", g.sats, g.hdop,
        pos, g.alt_m * 3.28084, g.speed_kmh * 0.621371, g.course_deg,
        g.year, g.month, g.day, g.hour, g.minute, g.second, (unsigned long)g.chars);
}

static void share_changed(lv_event_t *e)
{
    settings.share_position = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    settings_save();
    if (settings.share_position) mesh::send_position();
}

static void open(lv_obj_t *parent)
{
    s_label = lv_label_create(parent);
    s_share = lv_checkbox_create(parent);
    lv_checkbox_set_text(s_share, "Share my position on the mesh");
    if (settings.share_position) lv_obj_add_state(s_share, LV_STATE_CHECKED);
    lv_obj_align(s_share, LV_ALIGN_BOTTOM_LEFT, 0, -4);
    lv_obj_add_event_cb(s_share, share_changed, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_group_add_obj(ui_group(), s_share);
    refresh();
}

static void tick()
{
    static uint32_t last = 0;
    if (millis() - last > 1000) { last = millis(); refresh(); }
}

const App app_gps = {"GPS", LV_SYMBOL_GPS, open, nullptr, tick, nullptr};
