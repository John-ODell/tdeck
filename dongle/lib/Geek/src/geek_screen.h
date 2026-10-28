// The GEEK's 240x135 screen, drawn through one full-size TFT_eSprite so the whole frame
// can be dumped over the serial port ({"get":"screen"}, saved by common/tools/geekshot.py).
// Needs -DGEEK_LCD and the TFT_eSPI flags from common/geek.ini.
#pragma once
#include <TFT_eSPI.h>

extern TFT_eSPI geek_tft;
extern TFT_eSprite geek_spr; // GEEK_LCD_W x GEEK_LCD_H, 16-bit; pushSprite(0, 0) shows it

// Rotation 1 (USB plug on the left), black, sprite ready
void geek_screen_init();

// One JSON line: {"type":"screen","w":240,"h":135,"fmt":"rgb565","b64":"..."} of the
// sprite, big-endian RGB565 row by row
void geek_screen_json(Print &out);
