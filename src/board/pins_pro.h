// T-Deck Pro pin map (LilyGO examples/factory/utilities.h, branch HD-V2-250915 for
// V1.1, HD-V1-250326 for V1.0). Checked on a V1.1 unit by lilygo-A7682E's device probe.
// Included through src/board.h when BOARD_TDECK_PRO is defined.
#pragma once

#define BOARD_I2C_SDA       13
#define BOARD_I2C_SCL       14
#define BOARD_I2C_HZ        100000

// One SPI bus shared by the e-paper, the SD card and the LoRa radio
#define BOARD_SPI_SCK       36
#define BOARD_SPI_MOSI      33
#define BOARD_SPI_MISO      47

#define BOARD_EPD_CS        34
#define BOARD_EPD_DC        35
#define BOARD_EPD_BUSY      37      // LOW while the panel is busy

#define BOARD_SDCARD_CS     48

#define RADIO_CS_PIN        3
#define RADIO_RST_PIN       4
#define RADIO_DIO1_PIN      5
#define RADIO_BUSY_PIN      6
#define BOARD_LORA_EN       46      // radio is invisible while this is LOW

#define BOARD_GPS_EN        39      // active HIGH
#define BOARD_GPS_TX_PIN    43      // ESP TX -> GPS RX
#define BOARD_GPS_RX_PIN    44
#define BOARD_GPS_PPS       1
#define BOARD_GPS_BAUD      38400

// A7682E modem. Off by default: boost LOW, PWRKEY released (LOW).
#define BOARD_MODEM_RI      7
#define BOARD_MODEM_DTR     8
#define BOARD_MODEM_RST     9       // reset pulse is >= 2 s; never used for power
#define BOARD_MODEM_TX_PIN  10      // ESP TX -> modem RXD
#define BOARD_MODEM_RX_PIN  11
#define BOARD_MODEM_PWRKEY  40      // through an inverting transistor: HIGH = key pressed
#define BOARD_MODEM_BOOST_EN 41     // must be HIGH before PWRKEY

#define BOARD_BOOT_PIN      0
#define BOARD_TOUCH_INT     12
#define BOARD_KEYBOARD_INT  15
#define KEYBOARD_I2C_ADDR   0x34    // TCA8418
#define BOARD_IMU_INT       21
#define BOARD_HAPTIC_EN     2       // V1.1: DRV2605L supply. V1.0: motor MOSFET
#define BOARD_KB_BACKLIGHT  42

// Pins whose job depends on the revision. Drive none of them before board_rev() is known.
#define BOARD_PIN16         16      // V1.1: e-paper reset. V1.0: LTR-553 interrupt
#define BOARD_PIN38         38      // V1.1: touch reset.   V1.0: 1.8 V rail enable
#define BOARD_PIN45         45      // V1.1: front light.   V1.0: touch reset

#define SCREEN_W            240
#define SCREEN_H            320
