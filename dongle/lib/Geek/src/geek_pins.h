// Waveshare ESP32-S3-GEEK pins. The LCD's pins are TFT_eSPI build flags in common/geek.ini.
#pragma once

#define GEEK_BOOT_PIN 0 // BOOT button, low while held

// TF card slot, SDMMC 4-bit
#define GEEK_SD_CLK 36
#define GEEK_SD_CMD 35
#define GEEK_SD_D0 37
#define GEEK_SD_D1 33
#define GEEK_SD_D2 38
#define GEEK_SD_D3 34

// 1.14" ST7789, used in rotation 1 (USB plug on the left)
#define GEEK_LCD_W 240
#define GEEK_LCD_H 135
