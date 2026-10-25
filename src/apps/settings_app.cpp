#include "apps.h"
#include "ui/theme.h"
#include <Arduino.h>
#include "core/settings.h"
#include "mesh/mesh.h"
#include "mesh/protocol.h"
#include "hal/display.h"
#include "hal/power.h"

static lv_obj_t *s_long, *s_short, *s_chan, *s_preset, *s_power, *s_power_lbl, *s_hops, *s_relay, *s_tone;
static lv_obj_t *s_prec, *s_bright, *s_tball, *s_timeout, *s_tz, *s_stats;

static const int TZ_VALUES[] = {-300, -360, -420, -480, -540, -600, 0};
static const char *TZ_OPTS = "Eastern\nCentral\nMountain\nPacific\nAlaska\nHawaii\nUTC";
static const uint16_t TIMEOUTS[] = {15, 30, 60, 120, 300, 0};
static const char *TIMEOUT_OPTS = "15 s\n30 s\n1 min\n2 min\n5 min\nNever";
static const uint8_t PRECS[] = {32, 16, 13, 11};
static const char *PREC_OPTS = "Exact\n~300 m\n~1.4 km\n~6 km";

static lv_obj_t *row(lv_obj_t *parent, const char *label)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(r, 3, 0);
    lv_obj_t *l = lv_label_create(r);
    lv_label_set_text(l, label);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    return r;
}

static lv_obj_t *add_focus(lv_obj_t *o)
{
    lv_group_add_obj(ui_group(), o);
    lv_obj_add_event_cb(o, [](lv_event_t *e) { lv_obj_scroll_to_view_recursive(lv_event_get_target(e), LV_ANIM_ON); },
                        LV_EVENT_FOCUSED, nullptr);
    return o;
}

static lv_obj_t *text_row(lv_obj_t *parent, const char *label, const char *value, int max)
{
    lv_obj_t *r = row(parent, label);
    lv_obj_t *ta = lv_textarea_create(r);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, max);
    lv_textarea_set_text(ta, value);
    lv_obj_set_width(ta, 180);
    lv_obj_align(ta, LV_ALIGN_RIGHT_MID, 0, 0);
    return add_focus(ta);
}

static lv_obj_t *dropdown_row(lv_obj_t *parent, const char *label, const char *opts, int selected)
{
    lv_obj_t *r = row(parent, label);
    lv_obj_t *dd = lv_dropdown_create(r);
    lv_dropdown_set_options(dd, opts);
    lv_dropdown_set_selected(dd, selected < 0 ? 0 : selected);
    lv_obj_set_style_text_font(lv_dropdown_get_list(dd), &lv_font_montserrat_14, 0); // the list is not a child of the row
    lv_obj_set_width(dd, 150);
    lv_obj_align(dd, LV_ALIGN_RIGHT_MID, 0, 0);
    return add_focus(dd);
}

static void refresh_stats()
{
    const mesh::Stats &st = mesh::stats();
    lv_label_set_text_fmt(s_stats,
        "Node !%08x   %s\n%.3f MHz   hash 0x%02x\nRX %lu  bad %lu  dupe %lu  TX %lu  relayed %lu\nLast RSSI %.0f dBm  SNR %.1f dB   Batt %u mV",
        (unsigned)mesh::our_num(), mesh::radio_ok() ? "radio OK" : "NO RADIO", st.freq_mhz, st.channel_hash,
        (unsigned long)st.rx_ok, (unsigned long)st.rx_bad, (unsigned long)st.rx_dupe, (unsigned long)st.tx,
        (unsigned long)st.relayed, st.last_rssi, st.last_snr, power_battery_mv());
}

static void save_clicked(lv_event_t *)
{
    bool name_changed = strcmp(settings.long_name, lv_textarea_get_text(s_long)) ||
                        strcmp(settings.short_name, lv_textarea_get_text(s_short));
    uint8_t old_preset = settings.preset;
    char old_chan[sizeof(settings.channel_name)];
    strlcpy(old_chan, settings.channel_name, sizeof(old_chan));
    int8_t old_power = settings.tx_power;

    if (lv_textarea_get_text(s_long)[0]) strlcpy(settings.long_name, lv_textarea_get_text(s_long), sizeof(settings.long_name));
    if (lv_textarea_get_text(s_short)[0]) strlcpy(settings.short_name, lv_textarea_get_text(s_short), sizeof(settings.short_name));
    strlcpy(settings.channel_name, lv_textarea_get_text(s_chan), sizeof(settings.channel_name));
    settings.preset = lv_dropdown_get_selected(s_preset);
    settings.tx_power = lv_slider_get_value(s_power);
    settings.hop_limit = lv_dropdown_get_selected(s_hops) + 1;
    settings.rebroadcast = lv_obj_has_state(s_relay, LV_STATE_CHECKED);
    settings.position_precision = PRECS[lv_dropdown_get_selected(s_prec)];
    settings.brightness = lv_slider_get_value(s_bright);
    settings.trackball = lv_slider_get_value(s_tball);
    settings.chat_tone = lv_obj_has_state(s_tone, LV_STATE_CHECKED);
    settings.screen_timeout_s = TIMEOUTS[lv_dropdown_get_selected(s_timeout)];
    settings.tz_offset_min = TZ_VALUES[lv_dropdown_get_selected(s_tz)];
    settings_save();
    apply_timezone();
    display_set_brightness(settings.brightness);

    if (old_preset != settings.preset || strcmp(old_chan, settings.channel_name) || old_power != settings.tx_power)
        mesh::apply_settings();
    if (name_changed) mesh::send_nodeinfo(0xFFFFFFFF, false);
    refresh_stats();
    ui_toast("Saved");
}

static void open(lv_obj_t *parent)
{
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_right(col, 4, 0);
    // Labels, fields, dropdowns and buttons inherit this: the theme's mono font is too wide for the rows
    lv_obj_set_style_text_font(col, &lv_font_montserrat_14, 0);

    s_stats = lv_label_create(col);
    lv_obj_set_style_text_font(s_stats, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_stats, T_DIM, 0);
    refresh_stats();

    s_long = text_row(col, "Long name", settings.long_name, 39);
    s_short = text_row(col, "Short name", settings.short_name, 4);

    String presets;
    for (size_t i = 0; i < mt::NUM_PRESETS; i++) { if (i) presets += "\n"; presets += mt::PRESETS[i].name; }
    s_preset = dropdown_row(col, "Modem preset", presets.c_str(), settings.preset);
    s_chan = text_row(col, "Channel name", settings.channel_name, 11);
    lv_textarea_set_placeholder_text(s_chan, "(preset name)");

    lv_obj_t *r = row(col, "TX power");
    s_power = lv_slider_create(r);
    lv_slider_set_range(s_power, 2, 22);
    lv_slider_set_value(s_power, settings.tx_power, LV_ANIM_OFF);
    lv_obj_set_width(s_power, 110);
    lv_obj_align(s_power, LV_ALIGN_RIGHT_MID, -50, 0);
    s_power_lbl = lv_label_create(r);
    lv_label_set_text_fmt(s_power_lbl, "%d dBm", settings.tx_power);
    lv_obj_align(s_power_lbl, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(s_power, [](lv_event_t *) {
        lv_label_set_text_fmt(s_power_lbl, "%d dBm", (int)lv_slider_get_value(s_power));
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    add_focus(s_power);

    s_hops = dropdown_row(col, "Hop limit", "1\n2\n3\n4\n5\n6\n7", settings.hop_limit - 1);

    r = row(col, "Relay others' packets");
    s_relay = lv_switch_create(r);
    if (settings.rebroadcast) lv_obj_add_state(s_relay, LV_STATE_CHECKED);
    lv_obj_align(s_relay, LV_ALIGN_RIGHT_MID, 0, 0);
    add_focus(s_relay);

    int prec_idx = 2;
    for (int i = 0; i < 4; i++) if (PRECS[i] == settings.position_precision) prec_idx = i;
    s_prec = dropdown_row(col, "Shared position", PREC_OPTS, prec_idx);

    r = row(col, "Brightness");
    s_bright = lv_slider_create(r);
    lv_slider_set_range(s_bright, 1, 16);
    lv_slider_set_value(s_bright, settings.brightness, LV_ANIM_OFF);
    lv_obj_set_width(s_bright, 150);
    lv_obj_align(s_bright, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(s_bright, [](lv_event_t *) { display_set_brightness(lv_slider_get_value(s_bright)); },
                        LV_EVENT_VALUE_CHANGED, nullptr);
    add_focus(s_bright);

    r = row(col, "Trackball speed");
    s_tball = lv_slider_create(r);
    lv_slider_set_range(s_tball, 1, 5);
    lv_slider_set_value(s_tball, settings.trackball, LV_ANIM_OFF);
    lv_obj_set_width(s_tball, 150);
    lv_obj_align(s_tball, LV_ALIGN_RIGHT_MID, 0, 0);
    // Live, so the new speed can be felt before saving
    lv_obj_add_event_cb(s_tball, [](lv_event_t *) { settings.trackball = lv_slider_get_value(s_tball); },
                        LV_EVENT_VALUE_CHANGED, nullptr);
    add_focus(s_tball);

    r = row(col, "Message tone");
    s_tone = lv_switch_create(r);
    if (settings.chat_tone) lv_obj_add_state(s_tone, LV_STATE_CHECKED);
    lv_obj_align(s_tone, LV_ALIGN_RIGHT_MID, 0, 0);
    add_focus(s_tone);

    int to_idx = 2;
    for (int i = 0; i < 6; i++) if (TIMEOUTS[i] == settings.screen_timeout_s) to_idx = i;
    s_timeout = dropdown_row(col, "Screen off after", TIMEOUT_OPTS, to_idx);
    int tz_idx = 0;
    for (int i = 0; i < 7; i++) if (TZ_VALUES[i] == settings.tz_offset_min) tz_idx = i;
    s_tz = dropdown_row(col, "Time zone", TZ_OPTS, tz_idx);

    lv_obj_t *save = lv_btn_create(col);
    lv_obj_set_width(save, LV_PCT(100));
    lv_label_set_text(lv_label_create(save), LV_SYMBOL_SAVE " Save");
    lv_obj_add_event_cb(save, save_clicked, LV_EVENT_CLICKED, nullptr);
    add_focus(save);

    lv_obj_t *off = lv_btn_create(col);
    lv_obj_set_width(off, LV_PCT(100));
    lv_obj_set_style_bg_color(off, T_DANGER, 0);
    lv_obj_set_style_bg_color(off, T_DANGER_HI, LV_STATE_FOCUSED);
    lv_label_set_text(lv_label_create(off), LV_SYMBOL_POWER " Power off (click trackball to wake)");
    lv_obj_add_event_cb(off, [](lv_event_t *) {
        mesh::flush_storage();
        power_deep_sleep();
    }, LV_EVENT_CLICKED, nullptr);
    add_focus(off);
}

static void tick()
{
    static uint32_t last = 0;
    if (millis() - last > 2000) { last = millis(); refresh_stats(); }
}

const App app_settings = {"Settings", LV_SYMBOL_SETTINGS, open, nullptr, tick, nullptr};
