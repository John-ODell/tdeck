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

// Cross-app navigation
void chat_open_peer(uint32_t peer);
bool chat_viewing(uint32_t peer);
void map_center_on(uint32_t node_num);
