#include "hid_ble.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "link/link.h"
#include <NimBLEHIDDevice.h>

namespace hid_ble {

// Report 1: boot-style keyboard (modifiers, reserved, 6 keys). Report 2: 3-button mouse + wheel.
static const uint8_t REPORT_MAP[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,             // Keyboard, report 1
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, //   modifiers
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,                         //   reserved
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,             //   6 keys
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0xC0,
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02,             // Mouse, report 2
    0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, //   buttons
    0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x05, 0x81, 0x03,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,             //   x, y, wheel
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0xC0, 0xC0,
};

static NimBLEServer *s_server = nullptr;
static NimBLEHIDDevice *s_hid = nullptr;
static NimBLECharacteristic *s_kb = nullptr, *s_ms = nullptr;
static volatile bool s_connected = false;
static bool s_running = false;

class Callbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *) override { s_connected = true; Serial.println("[hid] host connected"); }
    void onDisconnect(NimBLEServer *) override { s_connected = false; Serial.println("[hid] host disconnected"); }
    void onAuthenticationComplete(ble_gap_conn_desc *desc) override
    {
        Serial.printf("[hid] auth %s, bonded %d\n", desc->sec_state.encrypted ? "encrypted" : "FAILED", desc->sec_state.bonded);
    }
};
static Callbacks s_cb;

bool start()
{
    if (s_running) return true;
    dongle::ble_sharing(true);
    NimBLEDevice::init("T-Deck Keyboard");
    NimBLEDevice::setSecurityAuth(true, false, true); // bond, no MITM (no display path), secure connections
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    s_server = NimBLEDevice::createServer();
    s_server->setCallbacks(&s_cb, false);
    s_hid = new NimBLEHIDDevice(s_server);
    s_kb = s_hid->inputReport(1);
    s_ms = s_hid->inputReport(2);
    s_hid->manufacturer()->setValue("tdeck");
    s_hid->pnp(0x02, 0xE502, 0xA111, 0x0210);
    s_hid->hidInfo(0x00, 0x01);
    s_hid->reportMap((uint8_t *)REPORT_MAP, sizeof(REPORT_MAP));
    s_hid->startServices();

    NimBLEAdvertising *adv = s_server->getAdvertising();
    adv->setAppearance(HID_KEYBOARD);
    adv->addServiceUUID(s_hid->hidService()->getUUID());
    adv->setScanResponse(true);
    s_running = adv->start();
    Serial.printf("[hid] advertising %s\n", s_running ? "started" : "FAILED");
    return s_running;
}

void stop()
{
    if (!s_running) return;
    s_running = false;
    s_connected = false;
    NimBLEDevice::deinit(true);
    dongle::ble_sharing(false);
    delete s_hid;
    s_hid = nullptr;
    s_server = nullptr;
    s_kb = s_ms = nullptr;
}

bool running() { return s_running; }
bool connected() { return s_running && s_connected; }

void forget_hosts()
{
    if (!s_running) return;
    NimBLEDevice::deleteAllBonds();
}

void set_battery(uint8_t percent)
{
    if (s_hid) s_hid->setBatteryLevel(percent > 100 ? 100 : percent);
}

void tap(uint8_t mod, uint8_t usage)
{
    if (!connected()) return;
    uint8_t r[8] = {mod, 0, usage, 0, 0, 0, 0, 0};
    s_kb->setValue(r, sizeof(r));
    s_kb->notify();
    memset(r, 0, sizeof(r));
    s_kb->setValue(r, sizeof(r));
    s_kb->notify();
}

void mouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel)
{
    if (!connected()) return;
    uint8_t r[4] = {buttons, (uint8_t)dx, (uint8_t)dy, (uint8_t)wheel};
    s_ms->setValue(r, sizeof(r));
    s_ms->notify();
}

} // namespace hid_ble
