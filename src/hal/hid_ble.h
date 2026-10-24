// Bluetooth LE HID: the T-Deck as a keyboard + mouse ("T-Deck Keyboard").
// Just Works bonding; bonds persist in NVS so hosts reconnect on their own.
#pragma once
#include <stdint.h>

namespace hid_ble {

bool start();          // advertise (NimBLE is initialised here and torn down in stop)
void stop();
bool running();
bool connected();
void forget_hosts();   // delete all bonds
void set_battery(uint8_t percent);

void tap(uint8_t mod, uint8_t usage);
void mouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel);

} // namespace hid_ble
