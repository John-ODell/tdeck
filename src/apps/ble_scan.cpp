#include "apps.h"
#include "ui/theme.h"
#include <Arduino.h>
#include <NimBLEDevice.h>

static lv_obj_t *s_list, *s_title;
static volatile bool s_done = false;
static bool s_scanning = false, s_inited = false;

static const NimBLEUUID MESHTASTIC_SVC("6ba1b218-15a8-461f-9fa8-5dcae273eafd");

// Identify common item trackers and Meshtastic nodes from the advertisement
const char *ble_classify(NimBLEAdvertisedDevice *d)
{
    for (uint8_t i = 0; i < d->getServiceUUIDCount(); i++) {
        NimBLEUUID u = d->getServiceUUID(i);
        if (u.equals(MESHTASTIC_SVC)) return "Meshtastic";
        if (u.equals(NimBLEUUID((uint16_t)0xFEED)) || u.equals(NimBLEUUID((uint16_t)0xFEEC))) return "TRACKER Tile";
    }
    for (uint8_t i = 0; i < d->getServiceDataCount(); i++) {
        NimBLEUUID u = d->getServiceDataUUID(i);
        if (u.equals(NimBLEUUID((uint16_t)0xFD5A))) return "TRACKER SmartTag";
        if (u.equals(NimBLEUUID((uint16_t)0xFEAA))) {
            std::string sd = d->getServiceData(i);
            if (!sd.empty() && (sd[0] == 0x40 || sd[0] == 0x41)) return "TRACKER Google Find";
        }
    }
    if (d->getManufacturerDataCount()) {
        std::string m = d->getManufacturerData();
        if (m.size() >= 3 && (uint8_t)m[0] == 0x4C && m[1] == 0x00) {
            if ((uint8_t)m[2] == 0x12) return "TRACKER Apple Find My";
            if ((uint8_t)m[2] == 0x07) return "Apple AirPods";
            return "Apple";
        }
        if (m.size() >= 2 && (uint8_t)m[0] == 0x06 && m[1] == 0x00) return "Microsoft";
        if (m.size() >= 2 && (uint8_t)m[0] == 0x75 && m[1] == 0x00) return "Samsung";
    }
    return "";
}

static void scan_done(NimBLEScanResults) { s_done = true; }

static void start_scan()
{
    if (!s_inited) {
        NimBLEDevice::init("");
        s_inited = true;
    }
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(90);
    scan->clearResults();
    s_done = false;
    s_scanning = scan->start(5, scan_done, false);
    lv_label_set_text(s_title, s_scanning ? LV_SYMBOL_REFRESH " Scanning 5 s..." : "BLE scan failed to start");
}

static void show_results()
{
    lv_obj_clean(s_list);
    lv_group_remove_all_objs(ui_group());
    NimBLEScanResults r = NimBLEDevice::getScan()->getResults();
    int n = r.getCount(), trackers = 0, mesh = 0;
    // Trackers first, then by signal
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < n; i++) {
            NimBLEAdvertisedDevice d = r.getDevice(i);
            const char *kind = ble_classify(&d);
            bool tracker = !strncmp(kind, "TRACKER", 7);
            if ((pass == 0) != tracker) continue;
            if (tracker) trackers++;
            if (!strcmp(kind, "Meshtastic")) mesh++;
            char line[120];
            std::string name = d.getName();
            snprintf(line, sizeof(line), "%s%s%s\n%d dBm  %s", name.empty() ? "(no name)" : name.c_str(),
                     kind[0] ? "  - " : "", kind, d.getRSSI(), d.getAddress().toString().c_str());
            lv_obj_t *b = ui_list_btn(s_list, tracker ? LV_SYMBOL_WARNING : LV_SYMBOL_BLUETOOTH, line);
            if (tracker) lv_obj_set_style_text_color(b, T_WARN, 0);
            lv_obj_add_event_cb(b, [](lv_event_t *) { start_scan(); }, LV_EVENT_CLICKED, nullptr);
        }
    }
    lv_label_set_text_fmt(s_title, "%d devices   %d trackers   %d Meshtastic   (r = rescan)", n, trackers, mesh);
    if (n == 0) ui_list_text(s_list, "Nothing found. Press r to rescan.");
}

static bool keys(uint32_t k)
{
    if (k == 'r' && !s_scanning) { start_scan(); return true; }
    return false;
}

static void open(lv_obj_t *parent)
{
    s_title = lv_label_create(parent);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_12, 0);
    s_list = lv_list_create(parent);
    lv_obj_set_size(s_list, LV_PCT(100), 196);
    lv_obj_set_y(s_list, 16);
    ui_set_app_key_hook(keys);
    start_scan();
}

static void tick()
{
    if (s_scanning && s_done) {
        s_scanning = false;
        show_results();
    }
}

static void close()
{
    if (s_inited) {
        NimBLEDevice::getScan()->stop();
        NimBLEDevice::deinit(true);
        s_inited = false;
    }
    s_scanning = false;
}

const App app_ble = {"BLE", LV_SYMBOL_BLUETOOTH, open, close, tick, nullptr};
