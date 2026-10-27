// T-Deck / T-Deck Plus pin map (from LilyGO examples/UnitTest/utilities.h).
// Included through src/board.h, which picks the board at compile time.
#pragma once

// Peripheral power rail: must be HIGH before touching any peripheral
#define BOARD_POWERON       10

#define BOARD_I2C_SDA       18
#define BOARD_I2C_SCL       8

#define BOARD_SPI_SCK       40
#define BOARD_SPI_MOSI      41
#define BOARD_SPI_MISO      38

#define BOARD_TFT_CS        12
#define BOARD_TFT_DC        11
#define BOARD_TFT_BL        42

#define BOARD_SDCARD_CS     39

#define RADIO_CS_PIN        9
#define RADIO_RST_PIN       17
#define RADIO_BUSY_PIN      13
#define RADIO_DIO1_PIN      45

#define BOARD_TOUCH_INT     16
#define BOARD_KEYBOARD_INT  46
#define KEYBOARD_I2C_ADDR   0x55

// Trackball: each pin toggles once per detent of movement
#define BOARD_TBOX_UP       3   // G01
#define BOARD_TBOX_DOWN     15  // G03
#define BOARD_TBOX_LEFT     1   // G04
#define BOARD_TBOX_RIGHT    2   // G02
#define BOARD_BOOT_PIN      0   // trackball click

#define BOARD_BAT_ADC       4

#define BOARD_GPS_TX_PIN    43
#define BOARD_GPS_RX_PIN    44

#define BOARD_I2S_WS        5
#define BOARD_I2S_BCK       7
#define BOARD_I2S_DOUT      6

#define SCREEN_W            320
#define SCREEN_H            240
