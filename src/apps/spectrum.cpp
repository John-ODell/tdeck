// Spectrum: three waterfalls on one canvas.
//   LoRa  902-928 MHz, SX1262 instantaneous RSSI stepped across the band (mesh pauses)
//   WiFi  2.4 GHz, promiscuous channel hopping, max RSSI per channel drawn as its 22 MHz hump
//   BLE   one lane per nearby device, time scrolling right to left
// Colour map and auto noise floor follow John-ODell/esp32-p4-rtl-sdr (view.c).
#include "apps.h"
#include "ui/theme.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <NimBLEDevice.h>
#include "link/link.h"
#include <algorithm>
#include "mesh/mesh.h"
#include "hal/input.h"
#include "core/settings.h"

static const int W = 320, INFO_H = 16, SPEC_H = 40, WF_H = 164, H = SPEC_H + WF_H;
static const float NONE = -200.0f; // "nothing here": always drawn black

enum Mode { MODE_LORA, MODE_WIFI, MODE_BLE, MODE_COUNT };
static Mode s_mode = MODE_LORA;
static bool s_running = false, s_idle = false;
static lv_obj_t *s_canvas, *s_info, *s_axis[4], *s_lane_lbl[16];
static lv_color_t *s_buf = nullptr;
static lv_color_t s_lut[256];
static float s_row[W], s_peak[W];
static int s_cmap = 0;
static float s_offset = 0;            // up/down shift of the colour range, dB
static float s_floor = 0;             // LoRa auto noise floor
static bool s_floor_valid = false;
static uint8_t s_markers[W];          // ticks along the top of the spectrum
static const int ROW_PX[] = {2, 2, 3}; // waterfall line height per mode

// ---- colour maps ---------------------------------------------------------

struct Stop { float t; uint8_t r, g, b; };
static const Stop CMAP_MATRIX[] = {{0, 0, 0, 0}, {.2f, 0, 30, 8}, {.4f, 0, 90, 30}, {.6f, 0, 170, 60},
                                   {.75f, 40, 230, 90}, {.88f, 150, 255, 170}, {1, 235, 255, 240}};
// "Studio" stops from the SDR repo
static const Stop CMAP_STUDIO[] = {{0, 0, 0, 12}, {.18f, 0, 8, 70}, {.36f, 0, 60, 170}, {.52f, 0, 170, 220},
                                   {.66f, 90, 230, 140}, {.78f, 250, 230, 40}, {.90f, 255, 110, 0}, {1, 255, 20, 30}};

static void build_lut()
{
    const Stop *st = s_cmap ? CMAP_STUDIO : CMAP_MATRIX;
    int n = s_cmap ? sizeof(CMAP_STUDIO) / sizeof(Stop) : sizeof(CMAP_MATRIX) / sizeof(Stop);
    for (int i = 0; i < 256; i++) {
        float t = i / 255.0f;
        int k = 0;
        while (k < n - 2 && t > st[k + 1].t) k++;
        float u = (t - st[k].t) / (st[k + 1].t - st[k].t);
        u = u < 0 ? 0 : u > 1 ? 1 : u;
        s_lut[i] = lv_color_make(st[k].r + (st[k + 1].r - st[k].r) * u, st[k].g + (st[k + 1].g - st[k].g) * u,
                                 st[k].b + (st[k + 1].b - st[k].b) * u);
    }
}

// ---- canvas ----------------------------------------------------------------

static void range(float *lo, float *hi)
{
    switch (s_mode) {
    case MODE_LORA: *lo = (s_floor_valid ? s_floor : -125) - 3 + s_offset; *hi = *lo + 45; break;
    case MODE_WIFI: *lo = -92 + s_offset; *hi = *lo + 52; break;
    default: *lo = -98 + s_offset; *hi = *lo + 55; break;
    }
}

static void clear_canvas()
{
    for (int i = 0; i < W * H; i++) s_buf[i] = T_BG;
    for (int x = 0; x < W; x++) s_peak[x] = NONE;
    s_floor_valid = false;
    lv_obj_invalidate(s_canvas);
}

static void push_row()
{
    float lo, hi;
    range(&lo, &hi);
    float span = hi - lo;

    // Waterfall: scroll down one row, newest on top
    const int px = ROW_PX[s_mode];
    memmove(s_buf + (SPEC_H + px) * W, s_buf + SPEC_H * W, (size_t)(WF_H - px) * W * sizeof(lv_color_t));
    for (int x = 0; x < W; x++) {
        float v = s_row[x];
        int i = v <= NONE ? 0 : (int)((v - lo) / span * 255.0f);
        lv_color_t c = s_lut[i < 0 ? 0 : i > 255 ? 255 : i];
        for (int y = 0; y < px; y++) s_buf[(SPEC_H + y) * W + x] = c;
    }

    // Live spectrum: bar per column, peak hold decaying 0.5 dB per row
    for (int i = 0; i < SPEC_H * W; i++) s_buf[i] = T_BG;
    for (int x = 0; x < W; x++) {
        float v = s_row[x];
        s_peak[x] = std::max(s_peak[x] - 0.5f, v);
        if (v > NONE) {
            int h = (int)((v - lo) / span * (SPEC_H - 4));
            h = h < 0 ? 0 : h > SPEC_H - 4 ? SPEC_H - 4 : h;
            for (int y = SPEC_H - 1; y >= SPEC_H - h; y--) s_buf[y * W + x] = T_SURFACE_HI;
            if (h) s_buf[(SPEC_H - h) * W + x] = T_DIM;
        }
        if (s_peak[x] > NONE) {
            int p = (int)((s_peak[x] - lo) / span * (SPEC_H - 4));
            p = p < 0 ? 0 : p > SPEC_H - 4 ? SPEC_H - 4 : p;
            s_buf[(SPEC_H - 1 - p) * W + x] = T_TEXT;
        }
        if (s_markers[x])
            for (int y = 0; y < 3; y++) s_buf[y * W + x] = s_markers[x] == 2 ? T_WARN : T_ACCENT;
    }
    lv_obj_invalidate(s_canvas);
}

static void set_axis(int i, const char *text, int x)
{
    if (!text) { lv_obj_add_flag(s_axis[i], LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_clear_flag(s_axis[i], LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_axis[i], text);
    lv_obj_update_layout(s_axis[i]);
    int w = lv_obj_get_width(s_axis[i]);
    x -= w / 2;
    lv_obj_set_pos(s_axis[i], x < 0 ? 0 : x + w > W ? W - w : x, INFO_H + 3); // above the bars
}

// ---- LoRa band ---------------------------------------------------------------

static const int LORA_BINS = 160;                 // 2 px each
static const float LORA_F0 = 902.0f, LORA_STEP = 26.0f / LORA_BINS;
static float s_lora[LORA_BINS];
static int s_lora_bin = 0;
static uint32_t s_lora_row_t0 = 0;

static bool lora_start()
{
    if (!mesh::sweep_begin(125.0f)) return false;
    s_lora_bin = 0;
    s_lora_row_t0 = millis();
    memset(s_markers, 0, sizeof(s_markers));
    int x = (int)((mesh::stats().freq_mhz - LORA_F0) / 26.0f * W);
    for (int i = -1; i <= 1; i++) if (x + i >= 0 && x + i < W) s_markers[x + i] = 2; // our channel
    set_axis(0, "902", 0);
    set_axis(1, "915", W / 2);
    set_axis(2, "928 MHz", W);
    set_axis(3, nullptr, 0);
    return true;
}

static void lora_tick()
{
    // A slice of bins per tick keeps the UI responsive; a full row takes ~250 ms
    uint32_t t0 = millis();
    while (millis() - t0 < 12) {
        uint32_t b0 = millis();
        s_lora[s_lora_bin] = mesh::sweep_rssi(LORA_F0 + LORA_STEP * (s_lora_bin + 0.5f));
        if (millis() - b0 > 50) Serial.printf("[spectrum] slow bin %d: %lu ms\n", s_lora_bin, (unsigned long)(millis() - b0));
        if (++s_lora_bin < LORA_BINS) continue;
        s_lora_bin = 0;
        // Noise floor: 20th percentile, smoothed
        float tmp[LORA_BINS];
        memcpy(tmp, s_lora, sizeof(tmp));
        std::nth_element(tmp, tmp + LORA_BINS / 5, tmp + LORA_BINS);
        float p20 = tmp[LORA_BINS / 5];
        s_floor = s_floor_valid ? s_floor * 0.95f + p20 * 0.05f : p20;
        s_floor_valid = true;
        float peak = -200;
        int peak_i = 0;
        for (int x = 0; x < W; x++) s_row[x] = s_lora[x * LORA_BINS / W];
        for (int i = 0; i < LORA_BINS; i++) if (s_lora[i] > peak) { peak = s_lora[i]; peak_i = i; }
        push_row();
        float lo_v = *std::min_element(s_lora, s_lora + LORA_BINS);
        Serial.printf("[spectrum] lora row %lu ms  min %.1f  max %.1f  floor %.1f\n",
                      (unsigned long)(millis() - s_lora_row_t0), lo_v, peak, s_floor);
        s_lora_row_t0 = millis();
        lv_label_set_text_fmt(s_info, "LoRa  floor %d  peak %d dBm @ %.2f MHz",
                              (int)s_floor, (int)peak, LORA_F0 + LORA_STEP * (peak_i + 0.5f));
        return;
    }
}

static void lora_stop() { mesh::sweep_end(); }

// ---- WiFi channels -------------------------------------------------------------

static const int WIFI_CH = 13;
static const uint32_t WIFI_DWELL_MS = 50, WIFI_ROW_MS = 200;
static float s_wval[WIFI_CH + 1];    // latest per-channel level, fading while unheard
static uint32_t s_wtotal[WIFI_CH + 1], s_wifi_row_at = 0;
static volatile int8_t s_wmax[WIFI_CH + 1];
static volatile uint32_t s_wcnt[WIFI_CH + 1];
static volatile uint8_t s_wch = 1;
static uint32_t s_hop_at = 0;

static void wifi_rx(void *buf, wifi_promiscuous_pkt_type_t)
{
    const wifi_promiscuous_pkt_t *p = (const wifi_promiscuous_pkt_t *)buf;
    uint8_t ch = s_wch;
    if (ch < 1 || ch > WIFI_CH) return;
    s_wcnt[ch]++;
    if (p->rx_ctrl.rssi > s_wmax[ch]) s_wmax[ch] = p->rx_ctrl.rssi;
}

static int wifi_x(float mhz) { return (int)((mhz - 2400.0f) / 84.0f * W); }

static void wifi_reset_counts()
{
    for (int c = 0; c <= WIFI_CH; c++) { s_wmax[c] = -128; s_wcnt[c] = 0; s_wval[c] = NONE; s_wtotal[c] = 0; }
}

static bool wifi_start()
{
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    wifi_reset_counts();
    wifi_promiscuous_filter_t f = {WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA};
    esp_wifi_set_promiscuous_filter(&f);
    esp_wifi_set_promiscuous_rx_cb(wifi_rx);
    if (esp_wifi_set_promiscuous(true) != ESP_OK) return false;
    s_wch = 1;
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    s_hop_at = s_wifi_row_at = millis();
    memset(s_markers, 0, sizeof(s_markers));
    for (int c = 1; c <= WIFI_CH; c++) s_markers[wifi_x(2407 + 5 * c)] = 1;
    set_axis(0, "1", wifi_x(2412));
    set_axis(1, "6", wifi_x(2437));
    set_axis(2, "11", wifi_x(2462));
    set_axis(3, "13", wifi_x(2472));
    return true;
}

static void wifi_tick()
{
    if (millis() - s_hop_at >= WIFI_DWELL_MS) {
        // Close the dwell on this channel: heard → its max RSSI, silent → fade out
        s_hop_at = millis();
        uint8_t ch = s_wch;
        if (s_wcnt[ch]) {
            s_wval[ch] = s_wmax[ch];
            s_wtotal[ch] += s_wcnt[ch];
        } else if (s_wval[ch] > NONE) {
            s_wval[ch] = s_wval[ch] - 4 < -100 ? NONE : s_wval[ch] - 4;
        }
        s_wmax[ch] = -128;
        s_wcnt[ch] = 0;
        s_wch = ch % WIFI_CH + 1;
        esp_wifi_set_channel(s_wch, WIFI_SECOND_CHAN_NONE);
    }
    if (millis() - s_wifi_row_at < WIFI_ROW_MS) return;
    s_wifi_row_at = millis();
    // Each channel is a hump across its 22 MHz with a quartic falloff (as scan_view.c)
    for (int x = 0; x < W; x++) {
        float f = 2400.0f + x * 84.0f / W, v = NONE;
        for (int c = 1; c <= WIFI_CH; c++) {
            if (s_wval[c] <= NONE) continue;
            float u = fabsf(f - (2407 + 5 * c)) / 11.0f;
            if (u >= 1) continue;
            v = std::max(v, s_wval[c] - 30.0f * u * u * u * u);
        }
        s_row[x] = v;
    }
    push_row();
    int busiest = 1;
    uint32_t total = 0;
    for (int c = 1; c <= WIFI_CH; c++) {
        total += s_wtotal[c];
        if (s_wtotal[c] > s_wtotal[busiest]) busiest = c;
    }
    lv_label_set_text_fmt(s_info, "WiFi  %u packets  busiest ch %d",
                          (unsigned)total, busiest);
}

static void wifi_stop()
{
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    WiFi.mode(WIFI_OFF);
}

// ---- BLE devices ---------------------------------------------------------------

static const int BLE_SLOTS = 16;
struct BleSlot { uint64_t addr; char name[20]; const char *kind; uint32_t seen; int8_t peak, last; bool used, fresh; };
static BleSlot s_slots[BLE_SLOTS];
static portMUX_TYPE s_ble_mux = portMUX_INITIALIZER_UNLOCKED;
static int s_sel = 0;
static uint32_t s_ble_row_at = 0;
static bool s_ble_inited = false;

class SpectrumScanCb : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice *d) override
    {
        uint64_t a = (uint64_t)d->getAddress();
        int8_t rssi = d->getRSSI();
        uint32_t now = millis();
        // A free slot or one silent for 30 s; when full, new devices wait
        portENTER_CRITICAL(&s_ble_mux);
        int slot = -1, spare = -1;
        for (int i = 0; i < BLE_SLOTS; i++) {
            if (s_slots[i].used && s_slots[i].addr == a) { slot = i; break; }
            if (spare < 0 && (!s_slots[i].used || now - s_slots[i].seen > 30000)) spare = i;
        }
        bool fresh = slot < 0 && spare >= 0;
        if (fresh) {
            slot = spare;
            BleSlot &n = s_slots[slot];
            memset(&n, 0, sizeof(n));
            n.addr = a;
            n.kind = "";
            n.peak = n.last = -128;
            n.used = n.fresh = true;
        }
        if (slot >= 0) {
            s_slots[slot].seen = now;
            if (rssi > s_slots[slot].peak) s_slots[slot].peak = rssi;
            s_slots[slot].last = rssi;
        }
        portEXIT_CRITICAL(&s_ble_mux);
        if (slot < 0) return;
        // Name and kind outside the critical section (they allocate)
        if (fresh || !s_slots[slot].name[0]) {
            std::string n = d->getName();
            const char *k = ble_classify(d);
            portENTER_CRITICAL(&s_ble_mux);
            if (s_slots[slot].addr == a) {
                strlcpy(s_slots[slot].name, n.c_str(), sizeof(s_slots[slot].name));
                if (fresh || k[0]) s_slots[slot].kind = k;
            }
            portEXIT_CRITICAL(&s_ble_mux);
        }
    }
};
static SpectrumScanCb s_scan_cb;

static bool ble_start()
{
    memset(s_slots, 0, sizeof(s_slots));
    if (!s_ble_inited) { dongle::ble_sharing(true); NimBLEDevice::init(""); s_ble_inited = true; }
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&s_scan_cb, true);
    scan->setActiveScan(true); // scan responses carry most names
    scan->setMaxResults(0);    // report and forget: nothing stored
    scan->setInterval(100);
    scan->setWindow(99);
    if (!scan->start(0, nullptr, false)) return false;
    s_ble_row_at = millis();
    for (int i = 0; i < 4; i++) set_axis(i, nullptr, 0);
    for (int i = 0; i < BLE_SLOTS; i++) lv_obj_clear_flag(s_lane_lbl[i], LV_OBJ_FLAG_HIDDEN);
    return true;
}

static void ble_info()
{
    portENTER_CRITICAL(&s_ble_mux);
    BleSlot s = s_slots[s_sel];
    portEXIT_CRITICAL(&s_ble_mux);
    if (!s.used) {
        lv_label_set_text_fmt(s_info, "BLE  #%d empty  " LV_SYMBOL_LEFT LV_SYMBOL_RIGHT " pick a device", s_sel + 1);
        return;
    }
    char mac[18];
    snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x", (int)(s.addr >> 40 & 0xff), (int)(s.addr >> 32 & 0xff),
             (int)(s.addr >> 24 & 0xff), (int)(s.addr >> 16 & 0xff), (int)(s.addr >> 8 & 0xff), (int)(s.addr & 0xff));
    lv_label_set_text_fmt(s_info, "#%d %d dBm %s %s", s_sel + 1, s.last,
                          s.name[0] ? s.name : mac, s.kind && s.kind[0] ? s.kind : "");
    lv_obj_set_style_text_color(s_info, s.kind && !strncmp(s.kind, "TRACKER", 7) ? T_WARN : T_TEXT, 0);
}

// Lanes: one 12 px row per device, a name column on the left, time scrolling
// right to left in 2 px steps every 250 ms (~29 s of history)
static const int LANE_H = 12, LANE_Y0 = 6, NAME_W = 92, STEP_PX = 2;

static void lane_fill(int lane, int x0, int x1, lv_color_t c)
{
    for (int y = LANE_Y0 + lane * LANE_H; y < LANE_Y0 + (lane + 1) * LANE_H - 2; y++)
        for (int x = x0; x < x1; x++) s_buf[y * W + x] = c;
}

static void ble_tick()
{
    if (millis() - s_ble_row_at < 250) return;
    s_ble_row_at = millis();
    BleSlot snap[BLE_SLOTS];
    uint32_t now = millis();
    portENTER_CRITICAL(&s_ble_mux);
    memcpy(snap, s_slots, sizeof(snap));
    for (int i = 0; i < BLE_SLOTS; i++) { s_slots[i].peak = -128; s_slots[i].fresh = false; }
    portEXIT_CRITICAL(&s_ble_mux);

    float lo, hi;
    range(&lo, &hi);
    for (int y = 0; y < H; y++)
        memmove(s_buf + y * W + NAME_W, s_buf + y * W + NAME_W + STEP_PX, (W - NAME_W - STEP_PX) * sizeof(lv_color_t));
    for (int i = 0; i < BLE_SLOTS; i++) {
        const BleSlot &b = snap[i];
        if (b.fresh) lane_fill(i, NAME_W, W, T_BG); // slot reused: drop the old device's history
        // Adverts come every 0.1-2 s, so hold the last reading rather than flicker
        int v = !b.used ? -128 : b.peak > -128 ? b.peak : now - b.seen < 2000 ? b.last : -128;
        int li = v == -128 ? 0 : (int)((v - lo) / (hi - lo) * 255.0f);
        lane_fill(i, W - STEP_PX, W, s_lut[li < 0 ? 0 : li > 255 ? 255 : li]);
        bool tracker = b.used && b.kind && !strncmp(b.kind, "TRACKER", 7);

        char label[16];
        if (!b.used) label[0] = 0;
        else if (b.name[0]) strlcpy(label, b.name, sizeof(label));
        else if (b.kind && b.kind[0]) strlcpy(label, tracker ? b.kind + 8 : b.kind, sizeof(label));
        else snprintf(label, sizeof(label), "%02x:%02x:%02x", (int)(b.addr >> 16 & 0xff), (int)(b.addr >> 8 & 0xff), (int)(b.addr & 0xff));
        if (strcmp(lv_label_get_text(s_lane_lbl[i]), label)) lv_label_set_text(s_lane_lbl[i], label);
        bool sel = i == s_sel;
        lv_obj_set_style_bg_opa(s_lane_lbl[i], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(s_lane_lbl[i], tracker ? T_WARN : T_ACCENT, 0);
        lv_obj_set_style_text_color(s_lane_lbl[i], sel ? T_ON_ACCENT : tracker ? T_WARN : now - b.seen < 5000 ? T_TEXT : T_DIM, 0);
    }
    lv_obj_invalidate(s_canvas);
    ble_info();
}

static void ble_stop()
{
    for (int i = 0; i < BLE_SLOTS; i++) lv_obj_add_flag(s_lane_lbl[i], LV_OBJ_FLAG_HIDDEN);
    if (!s_ble_inited) return;
    NimBLEDevice::getScan()->stop();
    NimBLEDevice::deinit(true);
    dongle::ble_sharing(false);
    s_ble_inited = false;
}

// ---- app -----------------------------------------------------------------------

static void stop_mode()
{
    if (!s_running) return;
    s_running = false;
    switch (s_mode) {
    case MODE_LORA: lora_stop(); break;
    case MODE_WIFI: wifi_stop(); break;
    default: ble_stop(); break;
    }
}

static void start_mode(Mode m)
{
    uint32_t t0 = millis();
    stop_mode();
    uint32_t t1 = millis();
    s_mode = m;
    clear_canvas();
    lv_obj_set_style_text_color(s_info, T_TEXT, 0);
    switch (m) {
    case MODE_LORA: s_running = lora_start(); break;
    case MODE_WIFI: s_running = wifi_start(); break;
    default: s_running = ble_start(); break;
    }
    Serial.printf("[spectrum] mode %d: stop %lu ms, start %lu ms, %s\n", (int)m, (unsigned long)(t1 - t0),
                  (unsigned long)(millis() - t1), s_running ? "ok" : "FAILED");
    static const char *names[] = {"LoRa 902-928 MHz (mesh paused)", "WiFi 2.4 GHz channels", "BLE devices"};
    lv_label_set_text_fmt(s_info, s_running ? "%s" : "%s  failed to start", names[m]);
    if (s_running) ui_toast(m == MODE_BLE ? "Click: next mode   c: colours   up/down: pick device"
                                          : "Click: next mode   c: colours   up/down: brightness");
}

static bool keys(uint32_t k)
{
    switch (k) {
    case LV_KEY_ENTER: start_mode((Mode)((s_mode + 1) % MODE_COUNT)); return true;
    case '1': case '2': case '3': start_mode((Mode)(k - '1')); return true;
    case 'c': s_cmap ^= 1; build_lut(); return true;
    case LV_KEY_PREV: case LV_KEY_NEXT:
        // BLE: pick a device lane; other modes: shift the colour range
        if (s_mode == MODE_BLE) {
            s_sel = (s_sel + (k == LV_KEY_NEXT ? 1 : BLE_SLOTS - 1)) % BLE_SLOTS;
            ble_info();
        } else {
            s_offset += k == LV_KEY_PREV ? 3 : -3;
        }
        return true;
    case LV_KEY_LEFT: case LV_KEY_RIGHT:
        if (s_mode == MODE_BLE) s_offset += k == LV_KEY_RIGHT ? 3 : -3;
        return true;
    }
    return false;
}

static void open(lv_obj_t *parent)
{
    lv_obj_set_style_pad_all(parent, 0, 0);
    if (!s_buf) s_buf = (lv_color_t *)ps_malloc(W * H * sizeof(lv_color_t));
    if (!s_buf) s_buf = (lv_color_t *)malloc(W * H * sizeof(lv_color_t));
    s_info = lv_label_create(parent);
    lv_obj_set_style_text_font(s_info, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(s_info, 4, 1);
    lv_label_set_long_mode(s_info, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(s_info, W - 8);
    if (!s_buf) {
        lv_label_set_text(s_info, "Not enough memory for the waterfall.");
        return;
    }
    s_canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(s_canvas, s_buf, W, H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(s_canvas, 0, INFO_H);
    for (int i = 0; i < 4; i++) {
        s_axis[i] = lv_label_create(parent);
        lv_obj_set_style_text_font(s_axis[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_axis[i], T_DIM, 0);
    }
    for (int i = 0; i < BLE_SLOTS; i++) {
        s_lane_lbl[i] = lv_label_create(parent);
        lv_obj_set_style_text_font(s_lane_lbl[i], &lv_font_unscii_8, 0);
        lv_label_set_long_mode(s_lane_lbl[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_size(s_lane_lbl[i], NAME_W - 4, LANE_H - 2);
        lv_obj_set_style_pad_top(s_lane_lbl[i], 1, 0);
        lv_obj_set_style_pad_left(s_lane_lbl[i], 2, 0);
        lv_obj_set_pos(s_lane_lbl[i], 0, INFO_H + LANE_Y0 + i * LANE_H);
        lv_label_set_text(s_lane_lbl[i], "");
        lv_obj_add_flag(s_lane_lbl[i], LV_OBJ_FLAG_HIDDEN);
    }
    build_lut();
    ui_set_app_key_hook(keys);
    s_idle = false;
    start_mode(s_mode);
}

static void tick()
{
    if (!s_buf || !s_canvas) return;
    // Stop with the screen so the mesh isn't left deaf; pick up again on wake
    bool idle = settings.screen_timeout_s && millis() - input_last_activity_ms() > settings.screen_timeout_s * 1000UL;
    if (idle && !s_idle) { s_idle = true; stop_mode(); return; }
    if (!idle && s_idle) { s_idle = false; start_mode(s_mode); return; }
    if (!s_running) return;
    switch (s_mode) {
    case MODE_LORA: lora_tick(); break;
    case MODE_WIFI: wifi_tick(); break;
    default: ble_tick(); break;
    }
}

static void close()
{
    stop_mode();
    s_canvas = nullptr;
}

const App app_spectrum = {"Spectrum", LV_SYMBOL_CHARGE, open, close, tick, nullptr};
