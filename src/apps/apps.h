#pragma once
#include <stdint.h>
#include "ui/ui.h"

extern const App app_about;
extern const App app_chat;
extern const App app_nodes;

// Cross-app navigation
void chat_open_peer(uint32_t peer);
bool chat_viewing(uint32_t peer);
