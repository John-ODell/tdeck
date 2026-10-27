# T-Deck Pro bring-up

Milestones 0 and 1 of the T-Deck Pro port: a board layer shared by both boards, the e-paper driver and display task, and a bring-up program that tests both on the Pro. The plan behind it is in lilygo-A7682E, "reports/TDeck Pro everyday OS plan.md".

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

## The e-paper display (milestone 1)

Three layers, all Pro-only:
- `src/hal/epd_uc8253.cpp` drives the UC8253 with the panel's factory OTP waveforms only. Plane writes are one bulk SPI transfer at 10 MHz, and the booster stays on through a burst. An allow list refuses every command the driver does not use, so the OTP-programming commands 0xA0, 0xA1 and 0xA3 can never be sent. The SPI lock is released while the panel is busy.
- `src/hal/epd_task.cpp` is a task on core 0 that owns a 1-bit shadow frame and a copy of what is on the glass. It decides when and how to draw, using the rules in `src/hal/epd_sched.h`, which the host tests check:
  - **Coalescing:** it starts once changes pause for 30 ms, or 250 ms after the first one, and never while LVGL is midway through a render.
  - **Skipping:** a change that leaves the frame identical to the glass is never drawn.
  - **Windowing:** a partial draws only the bytes that differ.
  - **Waveforms:** the first frame after boot is a slow full (about 3 s, clears ghosting). Every 10th partial becomes a fast full (about 1 s). `request_full()` asks for either.
  - **Power:** the panel powers off after 1.5 s idle.
- `src/hal/display_epd.cpp` implements `display.h` for LVGL. The flush converts each area to 1 bit into the shadow frame and returns at once, so `lv_timer_handler()` never waits for the panel.

The display test in the bring-up program stands in for the main loop. It polls every millisecond, runs LVGL and records the longest gap between iterations.

## Display result on a V1.1 unit (2026-10-27)

```
[EPD] first frame: 1 slow full in 3120 ms, 0 partials after it
[EPD] single 1: start after 30 ms, refresh 646 ms (busy 645 ms)
[EPD] burst: 39 keys in 4680 ms, 7 partials, 1 fast fulls, glass settled 910 ms after the last key
[EPD] burst: longest partial 647 ms, longest input-loop gap 8.7 ms
[EPD] idle 90 s: 0 refreshes, 90 skipped as unchanged, longest input-loop gap 11.1 ms
[EPD] final: 1 slow full in 3073 ms, 0 partials; panel powered no; refused commands 0
[EPD] PASS
```

| Limit (OS plan, milestone 1) | Result |
|---|---|
| Partial refresh at or under 680 ms | 646–647 ms; the factory waveform is 645 ms |
| Input loop never blocked over 20 ms during a refresh | 8.7 ms worst while typing, 11.1 ms while idle |
| At most 1 refresh in 90 s on an idle screen | 0, with 90 identical redraws skipped |
| Change to refresh start, panel idle | 30–39 ms |

A key pressed while a refresh is running waits for that refresh to finish, at most about 650 ms. The keys typed in that time are drawn together in the next one.

The screen content was not checked by eye in this run. The counts show that every change reached the panel and that identical frames did not.
