// TI TCA8418 keypad scanner on the T-Deck Pro (I2C 0x34, 4 rows x 10 columns, INT on
// GPIO15). Unlike the factory firmware, the key-event interrupt is on: the chip holds INT
// low while its 10-event FIFO has events, and the main loop drains it.
#pragma once
#include <stdint.h>

namespace tca {

bool init();
// Pops every queued event and calls on_event(code 1..80, pressed) for each, oldest first.
// Returns the number of events.
typedef void (*event_cb)(uint8_t code, bool pressed);
int drain(event_cb on_event);
bool pending();           // INT asserted or an edge seen since the last drain

struct Stats {
    uint32_t events = 0;
    uint32_t overflows = 0;  // FIFO overflowed: events were lost
    uint32_t edges = 0;      // INT falling edges
};
Stats stats();
int read_reg(uint8_t reg);   // for bring-up checks; -1 on I2C error

}  // namespace tca
