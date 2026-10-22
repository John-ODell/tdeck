#pragma once
#include <stdint.h>

void power_init();
uint16_t power_battery_mv();    // smoothed
uint8_t power_battery_percent();
bool power_usb_powered();       // heuristic: voltage above a full cell
void power_deep_sleep();        // wakes on trackball click
