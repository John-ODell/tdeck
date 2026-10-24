#pragma once
#include <stdint.h>
#include "ui/ui.h"

extern const App app_about;
extern const App app_chat;
extern const App app_nodes;
extern const App app_gps;
extern const App app_map;
extern const App app_wifi;
extern const App app_ble;
extern const App app_notes;
extern const App app_timer;
extern const App app_music;
extern const App app_files;
extern const App app_settings;
extern const App app_spectrum;
extern const App app_keyboard;

void music_loop();       // music: decode the next bit of the track from loop()

// Cross-app navigation
class NimBLEAdvertisedDevice;
const char *ble_classify(NimBLEAdvertisedDevice *d); // "TRACKER ...", "Meshtastic", maker, or ""
void chat_open_peer(uint32_t peer);
bool chat_viewing(uint32_t peer);
void map_center_on(uint32_t node_num);
