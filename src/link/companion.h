// Companion: what the dongle link carries besides keys. With the mesh relay on, every
// chat message (received or sent) goes to the dongle as a text event, and text the dongle
// hands us is transmitted on the mesh. Notes can be pushed to the dongle's card.
#pragma once
#include <stdint.h>

namespace companion {

void init();                 // restores the relay setting; starts the link if it is on
void tick();                 // drives the link and the notes sync
bool enabled();              // mesh relay on
void set_enabled(bool on);

bool sync_notes();           // false if the dongle is not online
const char *status();        // "" or what the sync is doing

} // namespace companion
