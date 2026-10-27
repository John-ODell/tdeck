// ESP-NOW link to the USB keyboard dongle (see link_proto.h; the dongle is in John-ODell/ESP-Geek).
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace dongle {

bool start();               // WiFi STA on LINK_CHANNEL + ESP-NOW; restores the paired dongle
void stop();                // says BYE first
void bye();                 // tell the dongle the silence that follows is on purpose (#166)
void tick();                // keepalive pings, pairing timeouts
bool running();
void ble_sharing(bool on);   // Bluetooth is up: WiFi must modem-sleep or the coexistence layer aborts

bool paired();
void peer_mac(uint8_t mac[6]);
bool online();              // dongle answered a ping in the last 3 s
bool old_firmware();        // the paired dongle spoke another link version in the last 10 s: reflash it
void forget();

// Pairing: collect HELLOs from unpaired dongles, then pick one
void pair_listen(bool on);
size_t candidates(uint8_t macs[][6], size_t max);
bool pair_with(const uint8_t mac[6]);   // sends PAIR; result arrives as pair_state()
enum PairState { PAIR_IDLE, PAIR_WAITING, PAIR_DONE, PAIR_FAILED };
PairState pair_state();   // DONE/FAILED hold until pair_listen() or the next pair_with()

bool tap(uint8_t mod, uint8_t usage);
bool mouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel);
bool lock_computer();       // the dongle types its lock shortcut (set on the dongle, Mac by default)

// Lock-when-away (#168): kept in NVS, sent to the dongle on change and on every reconnect
// once it has been set on this T-Deck. after_s = 0 turns it off. os is a LinkLockOs.
void set_lock(uint16_t after_s, uint8_t os, bool on_power_off);
uint16_t lock_after();      // readable while the link is off
uint8_t lock_os();
bool lock_on_power_off();
void power_off();           // LOCK if lock_on_power_off(), otherwise BYE; call before the power goes

// Blobs (mesh text, files): sent in parts, answered by the dongle with an ACK
typedef void (*blob_cb_t)(uint8_t kind, const uint8_t *data, size_t len);
void set_blob_cb(blob_cb_t cb);          // called from tick(), not from the radio task
bool send_blob(uint8_t kind, const void *data, size_t len, uint8_t *id_out = nullptr);
bool take_ack(uint8_t id, bool *ok);     // true once the ACK for that id arrived

} // namespace dongle
