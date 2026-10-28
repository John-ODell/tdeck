#ifdef GEEK_LCD
#include "geek_screen.h"
#include "geek_pins.h"
#include <mbedtls/base64.h>

TFT_eSPI geek_tft;
TFT_eSprite geek_spr(&geek_tft);

void geek_screen_init()
{
    geek_tft.init();
    geek_tft.setRotation(1);
    geek_tft.fillScreen(TFT_BLACK);
    geek_spr.setColorDepth(16);
    geek_spr.createSprite(GEEK_LCD_W, GEEK_LCD_H);
    geek_spr.fillSprite(TFT_BLACK);
}

void geek_screen_json(Print &out)
{
    const size_t n = GEEK_LCD_W * GEEK_LCD_H * 2;
    static char b64[((GEEK_LCD_W * GEEK_LCD_H * 2 + 2) / 3) * 4 + 1];
    size_t olen = 0;
    if (mbedtls_base64_encode((unsigned char *)b64, sizeof(b64), &olen, (const unsigned char *)geek_spr.getPointer(), n) != 0) return;
    out.printf("{\"type\":\"screen\",\"w\":%d,\"h\":%d,\"fmt\":\"rgb565\",\"b64\":\"", GEEK_LCD_W, GEEK_LCD_H);
    out.write((const uint8_t *)b64, olen);
    out.println("\"}");
}
#endif
