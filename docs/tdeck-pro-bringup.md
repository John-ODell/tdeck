# T-Deck Pro bring-up

Milestone 0 of the T-Deck Pro port: a board layer shared by both boards, and a bring-up program for the Pro. The plan behind it is in lilygo-A7682E, "reports/TDeck Pro everyday OS plan.md".

## The board layer

`src/board.h` picks the pin map at compile time: `src/board/pins_pro.h` when `BOARD_TDECK_PRO` is defined, `src/board/pins_plus.h` otherwise. Every file still includes `board.h`.

`src/board/board_api.h` is the same on both boards:
- `board_early_init()` runs first in `setup()`. On the Plus it is the bring-up that used to sit in `main.cpp`, unchanged.
- `board_model()`, `board_rev()`, `board_caps()` and `board_has()` say what the hardware has. The rules live in `src/board/caps.h`, which the host tests check.
- `board_lora_pins()` gives the radio pins, its enable pin and the TCXO voltage: 1.8 V on the Plus, 2.4 V on the Pro.
- `board_spi_lock()` and `board_spi_unlock()` guard the shared SPI bus. Nothing on the Plus uses them yet; the display task and the mesh adopt them in later milestones.
- `board_print_banner()` prints the model, revision, capabilities, memory and an I2C scan.

## Pro start-up order

1. Release pins held through deep sleep.
2. Rails default off: modem boost, PWRKEY (released, never held), GPS, haptics and keyboard backlight. LoRa power on.
3. Park the radio, e-paper and SD chip selects, then start SPI and I2C at 100 kHz.
4. Find the revision before touching GPIO16, 38 or 45, which do different jobs on V1.0 and V1.1. The V1.0 light sensor (0x23) is probed first, so a V1.0 motor is never driven. Then the DRV2605L (0x5A) marks a V1.1.
5. V1.1: front light off, e-paper reset pulse on GPIO16, touch reset pulse on GPIO38.

The factory firmware does the opposite on several of these. It drives every rail high and leaves the modem's PWRKEY pressed.

## The bring-up program

`env:tdeck-pro` builds only `src/board/` and `src/bringup/` until the Pro has display and input drivers. It runs the board layer, prints the banner and checks every part. It does not transmit on the radio, and the modem stays off. Send `b` over serial to run the checks again.

```bash
pio run -e tdeck-pro -t upload
```

```bash
pio device monitor -e tdeck-pro
```

## Result on a V1.1 unit (2026-10-27)

```
[BOARD] T-Deck Pro rev V1.1
[BOARD] ESP32-S3 rev 0, 240 MHz, flash 16 MB, PSRAM 8386295 bytes
[BOARD] caps: epaper touch haptics fuel-gauge charger-i2c imu modem gps lora keyboard sd
[BOARD] I2C 0x1A touch (Hynitron)
[BOARD] I2C 0x28 BHI260AP IMU
[BOARD] I2C 0x34 TCA8418 keyboard
[BOARD] I2C 0x55 BQ27220 gauge
[BOARD] I2C 0x5A DRV2605L haptics
[BOARD] I2C 0x6B BQ25896 charger
[BOARD] LoRa CS 3 DIO1 5 RST 4 BUSY 6 EN 46 TCXO 2.4 V
[CHECK] radio version "SX1261 V2D 2D02."
[CHECK] GPS NMEA at 38400      ok
[CHECK] modem                  off by default (not powered)
[BRINGUP] PASS (0 missing)
```

- PSRAM works on espressif32 6.3.0, the Plus's platform, with `qio_qspi`.
- The DRV2605L still answers at 0x5A with GPIO2 low, so GPIO2 is probably not its only supply.
