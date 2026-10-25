#pragma once

// Line commands over USB serial, for testing without touching the device:
//   open <app>   open an app by name
//   key <k>      enter, esc, up, down, left, right, or a single character
//   home         back to the launcher
//   shot         screenshot: "SHOT <w> <h>", base64 RGB565 (little-endian) lines, "END"
void console_poll();
