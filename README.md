# tdeck

A custom firmware for the **LilyGO T-Deck Plus** (ESP32-S3, SX1262, 915 MHz, internal antenna). It is keyboard-first and talks to stock **Meshtastic** nodes on the default US LongFast channel. It isn't a fork of Meshtastic: the radio protocol is reimplemented from the published source (see [docs/research](docs/research/report.md)).

> **Status: running on hardware.** Flashed and used on a T-Deck Plus since 2026-10-22: LongFast channel text and NodeInfo exchanged with a stock Meshtastic node, GPS, WiFi and BLE scans, Spectrum, the Keyboard app with the GEEK dongle, the shared drive, To-do, Timer, Compass and Music all checked on the device. Still open: a PKI DM against a real 2.7+ node and relaying a third node's packet (#26), battery calibration (#5).

## Apps

![Launcher](docs/screenshots/tdeck-launcher.png) ![Chat](docs/screenshots/tdeck-chat.png) ![GEEK dongle](docs/screenshots/geek-connected.png)

More screens in [`docs/screenshots/`](docs/screenshots/); the longer write-up is [`docs/tdeck.md`](docs/tdeck.md).


| App | What it does |
|---|---|
| **Chat** | `#LongFast` channel plus direct messages. Delivery status (… queued, ✓ sent, ✓✓ relayed/acked, ✕ failed). DMs are PKI-encrypted (X25519 + AES-256-CCM), which Meshtastic 2.7+ requires. A DM to a node whose key isn't known yet waits until its NodeInfo arrives. |
| **Nodes** | Every node heard: name, last heard, hops, SNR/RSSI, battery, distance and bearing. Actions: DM, request position, request info, show on map. |
| **Map** | Offline map from SD tiles in [Meshtastic MUI's layout](#map-tiles), or a distance grid without tiles. Shows you and every node with a position. `s` pulls new tiles from `map/` on the GEEK's card. |
| **GPS** | Auto-detects the L76K (9600) or u-blox M10 (38400) module. Shows fix, sats, HDOP, position, altitude, speed and UTC. Sets the clock. Has a "share my position" toggle (off by default). |
| **Compass** | Heading-up rose from the GPS course (no magnetometer, so no heading while stopped), speed, altitude, and a trip that keeps counting with the app closed: distance, moving time, max speed (`r` resets). `m` marks a waypoint, kept across reboots; the rose shows its direction and the page its distance and bearing. `c` clears it. |
| **WiFi** | Scanner: SSID, signal, channel, security, BSSID, networks per channel. |
| **BLE** | Scanner that flags item trackers (Apple Find My / AirTag, Tile, Samsung SmartTag, Google Find) and Meshtastic devices. |
| **Spectrum** | Three waterfalls. **LoRa** sweeps 902–928 MHz in 160 steps (about 3 rows/s) with an auto noise floor; mesh traffic pauses while it runs. **WiFi** hops channels 1–13 in promiscuous mode and draws each channel's 22 MHz hump. **BLE** gives each nearby device a lane, with time scrolling right to left and trackers in amber. Click: next mode. `c`: matrix or SDR colours. Up/down: brightness (LoRa, WiFi) or pick a device (BLE). |
| **Keyboard** | Use the T-Deck as a wireless keyboard and trackball mouse. **Bluetooth**: pair "T-Deck Keyboard" from a computer or phone (no PIN). **USB dongle**: an ESP32-S3/S2 running [`dongle/`](dongle/) plugs into the computer and types what the T-Deck sends over encrypted ESP-NOW. The trackball is a mouse (click = left button, double-click = scroll mode until the next click, press, release, press again and hold = drag) or arrow keys. Hold the trackball still for 3 s to stop typing (a long click does it in arrow-key mode). The touch screen is off while the app is open. With **Mesh: To dongle** on, chat is relayed to the dongle's serial port as JSON and lines sent to it go out on the mesh (`tools/meshterm.py`), even with the app closed; **Sync notes to dongle** copies the notes to the GEEK's TF card, which the computer sees as a USB drive. |
| **Notes** | Plain `.txt` notes in `/notes/` on the SD card (internal flash without a card). |
| **To-do** | A check list in `/todo.txt` (`[ ] item` / `[x] item` lines). Click toggles, `n` adds, `d` deletes, `c` clears the done ones. With the dongle online it syncs with `tdeck/todo.txt` on the GEEK's card: edit that file on the computer, `s` (or opening the app) pulls and merges it, every change pushes. A line deleted on either side stays deleted after the next sync. |
| **Files** | SD browser with sizes and free space. Views text files; deletes after pressing `d` twice; `p` pushes a file to the dongle's card. With a dongle paired, a **Dongle card** row opens the GEEK's TF card over the link: browse it, click a text file to read it, `g` pulls any file to `/dongle/` on the T-Deck. |
| **Timer** | Stopwatch with laps and a countdown. Left/right switch; click starts and pauses; `l` lap, `r` reset; up/down set the countdown. Both keep running with the app closed, and the countdown rings the speaker and wakes the screen from any app. |
| **Music** | MP3 and WAV from `/music` on the SD card through the speaker. Every folder in `/music` is a playlist, loose files are the `Music` playlist (the Pi's layout). Click = play/pause, left/right = track, up/down = volume, `p` = playlists, `x` = stop. Keeps playing with the app closed. `s` pulls everything new under `music/` on the GEEK's card. |
| **Settings** | Names, modem preset, channel name, TX power, hop limit, relaying, shared-position precision, brightness, screen timeout, US time zone, message tone and tone volume, radio stats, power off. |
| **About** | Version, MAC, battery, memory, detected peripherals. |

Mesh behaviour matches a Meshtastic `CLIENT`:
- Relays other nodes' packets using the SNR-weighted contention window, and drops its relay if another node relays first.
- Dedupes on (from, id).
- ACKs DMs and retries its own up to 3 times.
- Broadcasts NodeInfo (with its public key) at boot and every 3 h.
- Sends battery telemetry every 30 min, and its position every 15 min if sharing is on.

## Controls

| Input | Action |
|---|---|
| Trackball up / down | Move focus (scroll in Chat, Files viewer, Map) |
| Trackball left / right | Adjust sliders and dropdowns, move the text cursor, pan the map |
| Trackball click | Select / Enter |
| Trackball **long click** | Back |
| Trackball **held 2 s** | Lock: screen off, keys ignored |
| Trackball **held 1 s while locked** | Unlock |
| Keyboard | Type; Enter sends a message |
| `Alt+B` | Keyboard backlight (handled by the keyboard itself) |
| Map: `+` `-` `c` `n` | Zoom in / out, centre on me, jump to the next node |
| Notes: `n` · Files: `d` `d` · WiFi/BLE: `r` | New note · delete · rescan |
| Touch | Tap anything |

The screen fades out after the timeout (default 10 s) and the T-Deck locks: keys and trackball rolls do nothing, so a pocket press changes nothing. Hold the trackball button about a second to unlock, or two seconds while awake to lock at once. An incoming message or a finished timer lights the screen for a look but keeps the lock.

## Flash it

You need [PlatformIO](https://platformio.org/) (`uv tool install platformio` or `pip install platformio`) and a USB-C **data** cable.

```bash
scripts/build_release.sh
```

```bash
scripts/flash.sh
```

`build_release.sh` writes `release/tdeck-plus-<version>-merged.bin` (bootloader, partitions and app; flash at **0x0**) and an app-only `-app.bin` (0x10000, for Launcher/OTA). `flash.sh` flashes the newest merged image at 0x0 and refuses anything else. Writing a merged image at 0x10000 is a known way to brick a T-Deck.

**If flashing can't connect** (common when Meshtastic or another firmware is running), enter download mode:
1. Turn the T-Deck off (left-side switch).
2. Hold the trackball down.
3. Turn it on and keep holding for 2–3 s.
4. Release. The screen stays black: that's download mode.
5. Run `scripts/flash.sh`, then press reset or power-cycle.

To name a port: `scripts/flash.sh release/<file>-merged.bin /dev/cu.usbmodem101`.

**Going back to stock Meshtastic:** use [flasher.meshtastic.org](https://flasher.meshtastic.org/) with download mode as above. The LilyGO factory test images are in the [upstream repo](https://github.com/Xinyuan-LilyGO/T-Deck/tree/master/firmware).

## Map tiles

Copy 256 px PNG slippy tiles to the SD card (FAT32 or exFAT) as either:

```
/map/{z}/{x}/{y}.png
/maps/<style>/{z}/{x}/{y}.png
```

This is the same layout Meshtastic MUI uses, so MUI tile packs work as-is. With a dongle paired, put the same `map/` tree on the GEEK's card (it is a USB drive on the computer) and press `s` in the Map app to pull it over the air. 8-bit palette PNGs decode fastest. Respect the tile provider's terms; OpenStreetMap's public tile servers forbid bulk downloads.

## Serial console

At 115200 baud, over the USB port, one command per line:

| Command | Does |
|---|---|
| `open <app>` | Opens an app by name, e.g. `open Spectrum` |
| `key <k>` | Injects a key: `enter`, `esc`, `up`, `down`, `left`, `right` or one character |
| `home` | Back to the launcher |
| `ls <dir>` / `cat <file>` | List a directory / print a file from the SD card (or internal flash without one; `flash:/x` forces flash) |
| `shot` | Screenshot: `SHOT <w> <h>`, base64 RGB565 little-endian lines, then `END` |
| `rssi` | SX1262 instantaneous RSSI at several settle times (sweep diagnostics) |

Output only arrives if the host opened the port before the device booted, so reset after connecting.

## Build and test

```bash
~/.local/bin/pio run
```

```bash
test/host/run.sh
```

```bash
test/host/run_crypto.sh /path/to/mbedtls-v2.28.8
```

- `test/host/run.sh` checks the frequency slot (906.875 MHz), channel hash (0x08), nonce layout, header flags, protobuf codec, and AES-CTR against OpenSSL.
- `test/host/run_crypto.sh` builds `src/mesh/crypto.cpp` against upstream mbedtls and checks X25519 against OpenSSL, plus the PKI round trip and tamper rejection. It needs an mbedtls 2.28 checkout ([Mbed-TLS/mbedtls](https://github.com/Mbed-TLS/mbedtls), tag `v2.28.8`).
- `scripts/qemu_smoke.sh` boots the merged image in [Espressif QEMU](https://github.com/espressif/qemu/releases) for 45 s and fails on any panic.

## Layout

```
src/board.h            pin map (from LilyGO utilities.h, cross-checked with Meshtastic's variant)
src/hal/               display + backlight, keyboard/trackball/touch, battery, GPS
src/mesh/              protocol constants, protobuf codec, crypto, router + radio
src/core/              settings (NVS), node DB and message log (LittleFS), SD
src/ui/                launcher, status bar, toasts, helpers
src/apps/              one file per app
lib/                   LilyGO's preconfigured TFT_eSPI and SensorLib (don't replace with registry copies)
docs/research/         protocol, hardware and landscape research behind the design
```

## Not verified yet

All of the following need a real device:

- On-air interop with a stock Meshtastic node (channel text, NodeInfo, Position, relaying).
- PKI DMs against a real node. X25519 matches OpenSSL; AES-CCM is only round-trip tested.
- Display orientation and colours, touch mirroring, trackball direction and step size.
- First-boot LittleFS format on the `spiffs` partition (QEMU can't emulate the flash writes).
- GPS detection on both module variants, and map tile rendering speed.
- Battery calibration: the ADC multiplier may need 2.11 instead of 2.0 (#5).
- Meshtastic 2.8 XEdDSA signatures aren't implemented. 2.8 nodes are believed to accept unsigned packets by default.
