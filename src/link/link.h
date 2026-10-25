// ESP-NOW link to the USB keyboard dongle (see link_proto.h and dongle/).
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace dongle {

bool start();               // WiFi STA on LINK_CHANNEL + ESP-NOW; restores the paired dongle
void stop();
void tick();                // keepalive pings, pairing timeouts
bool running();

bool paired();
void peer_mac(uint8_t mac[6]);
bool online();              // dongle answered a ping in the last 3 s
void forget();

// Pairing: collect HELLOs from unpaired dongles, then pick one
void pair_listen(bool on);
size_t candidates(uint8_t macs[][6], size_t max);
bool pair_with(const uint8_t mac[6]);   // sends PAIR; result arrives as pair_state()
enum PairState { PAIR_IDLE, PAIR_WAITING, PAIR_DONE, PAIR_FAILED };
PairState pair_state();   // DONE/FAILED hold until pair_listen() or the next pair_with()

bool tap(uint8_t mod, uint8_t usage);
bool mouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel);

} // namespace dongle
