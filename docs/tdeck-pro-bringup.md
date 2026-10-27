# T-Deck Pro bring-up

Milestones 0 to 2 of the T-Deck Pro port: a board layer shared by both boards, the e-paper driver and display task, keyboard and touch input, and a bring-up program that tests them on the Pro. The plan behind it is in lilygo-A7682E, "reports/TDeck Pro everyday OS plan.md".

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

`env:tdeck-pro` builds the board layer, the Pro display and input drivers and `src/bringup/`. It runs the board layer, prints the banner and checks every part, then checks the keyboard and touch chips and leaves a typing screen up. It does not transmit on the radio, and the modem stays off.

| Serial key | Does |
|---|---|
| `b` | Run the part checks again |
| `d` | Run the display test (about 2.5 minutes) |
| `s` | Print the keyboard and touch counters |

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

## Keyboard and touch (milestone 2)

`src/hal/input_pro.cpp` implements `input.h` for the Pro, so the Plus apps get the same keys and pointer. The trackball calls do nothing.

- **Keyboard:** `src/hal/tca8418.cpp` drives the TCA8418.
  - The key-event interrupt is on, on GPIO15. The factory firmware polls with it off.
  - The main loop drains the chip's 10-event FIFO when INT asserts, and every 50 ms anyway. FIFO overflows are counted.
  - Unused pins make no events or interrupts. The factory firmware enabled both on floating pins.
- **Keymap:** `src/hal/keymap_pro.h` is pure code, host-tested in `test/host/test_keymap_pro.cpp`.

  | Layer | Gives |
  |---|---|
  | Shift | capitals |
  | Sym | the symbol printed on each key; Sym+Mic is `0` |
  | Alt | navigation: E up, X down, S left, F right, Q Esc, T Tab (next), Backspace delete |

  - A modifier works held, tapped once (next key only), or tapped twice (locked until tapped again).
  - Mic is Esc (back), since the board has no other back key.
  - Held characters, Backspace and arrows repeat after 450 ms, every 60 ms.
- **Touch:** `src/hal/touch_cst66xx.cpp` follows LilyGo's CST66xx driver. It reads a report on each INT edge, checks the 0x55-seeded sum and acknowledges. The panel reports portrait 240x320, so no transform is applied.
- **BOOT button:** a short press is Esc. Held a second, it locks, or unlocks when locked, like the Plus trackball click.

### Result on a V1.1 unit (2026-10-27)

```
[CHECK] TCA8418 interrupt on   ok
[CHECK] TCA8418 matrix 4x10    ok
[CHECK] keyboard INT idle high ok
[CHECK] touch IC type 0xCACA220E fw 0x01010001 project 0x02511107 res 240x320 tx 10 rx 8 keys 0
[CHECK] touch info signature   ok
[CHECK] no touch INT while idle ok
[CHECK] no key INT while idle  ok
[INPUT] checks PASS (0 failed)
```

### Hand check (still to do)

These need someone at the keyboard. Flash `env:tdeck-pro` and open the monitor. Each key prints a `[KEY]` line and each row tap a `[TAP]` line.
1. Press all 35 keys, then each one with Shift, Sym and Alt held, and compare against the legends.
2. While the screen is refreshing, type 10 keys as fast as possible. All 10 must appear, and `s` must show 0 overflows and 0 dropped.
3. Tap the middle of each of the six rows. Each `[TAP]` line must name the row tapped.
4. Hold BOOT a second to lock, type (nothing should appear), then hold it again to unlock.
