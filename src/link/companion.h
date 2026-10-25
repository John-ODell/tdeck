// Companion: what the dongle link carries besides keys. With the mesh relay on, every
// chat message (received or sent) goes to the dongle as a text event, and text the dongle
// hands us is transmitted on the mesh. Files move both ways: notes and any other file can
// be pushed to the dongle's card, and the card can be listed and pulled from.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace companion {

void init();                 // restores the relay setting; starts the link if it is on
void tick();                 // drives the link and the file transfers
bool enabled();              // mesh relay on
void set_enabled(bool on);

bool sync_notes();           // pushes /notes/* to tdeck/notes/ on the card
const char *status();        // "" or what the transfer is doing
bool busy();                 // a transfer is running

// The dongle's card. Paths are relative to its root; callbacks run from tick().
struct FsEntry { char name[48]; uint32_t size; bool dir; };
typedef void (*fs_list_cb_t)(bool ok, const FsEntry *entries, size_t count);
typedef void (*fs_done_cb_t)(bool ok);
bool fs_list(const char *path, fs_list_cb_t cb);
bool fs_get(const char *remote, const char *local, fs_done_cb_t cb);  // saves on the T-Deck's store
bool fs_put(const char *local, const char *remote, fs_done_cb_t cb);  // local file to the card

} // namespace companion
