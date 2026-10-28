# tdeck

A custom firmware for the **LilyGO T-Deck Plus** that talks to stock **Meshtastic** nodes, and a USB dongle that turns the T-Deck into a wireless keyboard, mouse, mesh terminal and shared drive for a computer.

It is not a fork of Meshtastic. The radio protocol is reimplemented from the published source (see [docs/research](docs/research/report.md)) on an ESP32-S3 Arduino build with LVGL, so the apps are free to be whatever a pocket deck should have: chat, nodes, an offline map, GPS and compass, WiFi, BLE and LoRa scanners, notes, to-do, files, a timer, music, and the Keyboard app. The look is phosphor green on black.

**Status:** in daily use on a T-Deck Plus since 2026-10-22. Channel text, direct messages, relaying and map tiles are confirmed against stock Meshtastic nodes (#26, #129). What is not done is listed at the [end](#what-is-not-done).

![Launcher](docs/screenshots/tdeck-launcher.png) ![Chat](docs/screenshots/tdeck-chat.png) ![GEEK dongle](docs/screenshots/geek-connected.png)

More screens in [`docs/screenshots/`](docs/screenshots/); the longer write-up is [`docs/tdeck.md`](docs/tdeck.md).

## Hardware

This is built and tested with two boards:

| Board | Role | What it has |
|---|---|---|
| [LilyGO T-Deck Plus](https://lilygo.cc/products/t-deck-plus) | the device | ESP32-S3, 2.8" 320×240 touch screen, keyboard, trackball, SX1262 LoRa (915 MHz, internal antenna), GPS, microSD, speaker, 2000 mAh battery |
| [Waveshare ESP32-S3-GEEK](https://www.waveshare.com/wiki/ESP32-S3-GEEK) | the dongle | ESP32-S3 in a USB-A stick with a 1.14" screen and a TF card slot |

The original T-Deck (no GPS, no battery) uses the same pin map and should run the same image, but only the Plus has been tried.

The dongle firmware lives in [John-ODell/ESP-Geek](https://github.com/John-ODell/ESP-Geek) under `firmware/tdeck-dongle`, with a PlatformIO env per board:

| Env | Board | Gets |
|---|---|---|
| `geek` | Waveshare ESP32-S3-GEEK | everything: keyboard, mouse, mesh terminal, the TF card as a shared drive, link status and the last keys on its screen, lock-when-away |
| `s3` | any ESP32-S3 board with the USB port on the native pins | keyboard, mouse, mesh terminal, lock-when-away; no drive, no screen |
| `s2` | any ESP32-S2 board | the same as `s3` |

The GEEK is the one to get: the shared drive is what makes notes, to-do, music and map tiles flow between the T-Deck and the computer, and it sits in a USB port all day. A plain ESP32-S3 board is fine for the keyboard alone.

## Apps

| App | What it does |
|---|---|
| **Chat** | `#LongFast` channel plus direct messages (**New DM** or `n` picks a node from the ones heard). Delivery status (… queued, ✓ sent, ✓✓ relayed/acked, ✕ failed). DMs are PKI-encrypted (X25519 + AES-256-CCM), which Meshtastic 2.7+ requires. A DM to a node whose key isn't known yet waits until its NodeInfo arrives. |
| **Nodes** | Every node heard: name, last heard, hops, SNR/RSSI, battery, distance and bearing. Actions: DM, request position, request info, show on map. |
| **Map** | Offline map from SD tiles in [Meshtastic MUI's layout](#map-tiles), or a distance grid without tiles. Shows you and every node with a position, with a breadcrumb trail of where each has been since boot. `s` pulls new tiles from `map/` on the GEEK's card. |
| **GPS** | Auto-detects the L76K (9600) or u-blox M10 (38400) module. Shows fix, sats, HDOP, position, altitude, speed and UTC. Sets the clock. Has a "share my position" toggle (off by default). |
| **Compass** | Heading-up rose from the GPS course (no magnetometer, so no heading while stopped), speed, altitude, and a trip that keeps counting with the app closed: distance, moving time, max speed (`r` resets). `m` marks a waypoint, kept across reboots; the rose shows its direction and the page its distance and bearing. `c` clears it. |
| **WiFi** | Scanner: SSID, signal, channel, security, BSSID, networks per channel. |
| **BLE** | Scanner that flags item trackers (Apple Find My / AirTag, Tile, Samsung SmartTag, Google Find) and Meshtastic devices. |
| **Spectrum** | Three waterfalls. **LoRa** sweeps 902–928 MHz in 160 steps (about 3 rows/s) with an auto noise floor; mesh traffic pauses while it runs. **WiFi** hops channels 1–13 in promiscuous mode and draws each channel's 22 MHz hump. **BLE** gives each nearby device a lane, with time scrolling right to left and trackers in amber. Click: next mode. `c`: matrix or SDR colours. Up/down: brightness (LoRa, WiFi) or pick a device (BLE). |
| **Keyboard** | The T-Deck as a wireless keyboard and mouse for a computer, over Bluetooth or the dongle. Its own section is [below](#the-keyboard-app-and-the-dongle). |
| **Notes** | Plain `.txt` notes in `/notes/` on the SD card (internal flash without a card). |
| **To-do** | A check list in `/todo.txt` (`[ ] item` / `[x] item` lines). Click toggles, `n` adds, `d` deletes, `c` clears the done ones. With the dongle online it syncs with `tdeck/todo.txt` on the GEEK's card: edit that file on the computer, `s` (or opening the app) pulls and merges it, every change pushes. A line deleted on either side stays deleted after the next sync. |
| **Files** | SD browser with sizes and free space. Views text files; deletes after pressing `d` twice; `p` pushes a file to the dongle's card. With a dongle paired, a **Dongle card** row opens the GEEK's TF card over the link: browse it, click a text file to read it, `g` pulls any file to `/dongle/` on the T-Deck. |
| **Timer** | Stopwatch with laps and a countdown. Left/right switch; click starts and pauses; `l` lap, `r` reset; up/down set the countdown. Both keep running with the app closed, and the countdown rings the speaker and wakes the screen from any app. |
| **Music** | MP3 and WAV from `/music` on the SD card through the speaker. Every folder in `/music` is a playlist, loose files are the `Music` playlist. Click = play/pause, left/right = track, up/down = volume, `p` = playlists, `x` = stop. Keeps playing with the app closed. `s` pulls everything new under `music/` on the GEEK's card. |
| **Settings** | Names, modem preset, channel name, TX power, hop limit, relaying, shared-position precision, brightness, screen timeout, US time zone, message tone and tone volume, the computer-lock shortcut, radio stats, power off. |
| **About** | Version, MAC, battery, memory, detected peripherals. |

Mesh behaviour matches a Meshtastic `CLIENT`:
- Relays other nodes' packets using the SNR-weighted contention window, and drops its relay if another node relays first.
- Dedupes on (from, id).
- ACKs DMs and retries its own up to 3 times.
- Broadcasts NodeInfo (with its public key) at boot and every 3 h.
- Sends battery telemetry every 30 min, and its position every 15 min if sharing is on.

## The Keyboard app and the dongle

Two ways to reach a computer:

- **Bluetooth**: pair "T-Deck Keyboard" from a computer or phone (no PIN). Keyboard and mouse only.
- **USB dongle**: the dongle plugs into the computer and types what the T-Deck sends over ESP-NOW. Pairing is an X25519 key exchange; after that every packet is AES-CCM encrypted and carries an HMAC tag and a counter, so a recorded packet cannot be replayed. Keys travel as press-and-release taps, so a lost packet never leaves a key stuck down.

On the typing screen the trackball is a mouse (click = left button, press and hold = drag, double-click = scroll mode until the next click) or arrow keys. Hold the trackball still for 3 s to stop typing (a long click does it in arrow-key mode). The screen is an editor with a caret: a tap in the text puts it on that letter and the computer's cursor follows, typing inserts there, backspace deletes before it, **Clear** empties the box (the computer keeps its text).

What the dongle link carries besides keys:

- **Mesh chat on the desk** (`Mesh chat: To dongle`): every chat message is relayed to the dongle's USB serial port as a JSON line and lines sent to it go out on the mesh, even with the app closed. `tools/meshterm.py` makes the computer a Meshtastic terminal with no app. Messages that arrive while the dongle is unplugged are forwarded when it returns (the last 20).
- **Shared drive** (GEEK only): the GEEK's TF card is a USB drive on the computer and a folder on the T-Deck at the same time. **Sync** copies the notes to it, To-do syncs both ways, Music and Map pull from it, Files browses it over the air.
- **Lock the computer**: **Lock when away** (Off, 10, 20 or 60 s) has the dongle lock the computer that long after the T-Deck goes quiet, and the **Lock** button locks it now. The shortcut (Mac Ctrl+Cmd+Q, PC Win+L, or Report only, which just logs it on the dongle's serial port) and whether **Power off** locks the computer are in Settings. Over Bluetooth the T-Deck taps the shortcut itself, and only on the button. With the lock on, powering the T-Deck off or reflashing it locks the computer too; set it to Off first when that is not what you want.

**Pairing:** plug the dongle in with its screen saying "Pairing" (hold its BOOT button for 2 s to get there from a paired one), then on the T-Deck open **Keyboard**, set Send to **USB dongle** and press **Pair**. **Forget** needs a second press.

**Versions:** the T-Deck owns the link protocol ([`src/link/link_proto.h`](src/link/link_proto.h)) and ESP-Geek keeps a copy. Both ends must be built from the same `LINK_VERSION`; a mismatch is logged on the dongle's serial port and shown on the Keyboard page. The lock feature needs a dongle built from ESP-Geek `main` on or after 2026-10-26; the [`tdeck-dongle-v1.0.0`](https://github.com/John-ODell/ESP-Geek/releases/tag/tdeck-dongle-v1.0.0) release does everything else. See [`dongle/`](dongle/) for the table.

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

The screen fades out after the timeout (default 10 s) and the T-Deck locks: keys, taps and trackball rolls do nothing, so a pocket press changes nothing. Hold the trackball button about a second to unlock, or two seconds while awake to lock at once. An incoming message or a finished timer lights the screen for a look but keeps the lock.

## Flash it

You need [PlatformIO](https://platformio.org/) (`uv tool install platformio` or `pip install platformio`) and a USB-C **data** cable.

```bash
scripts/build_release.sh
```

```bash
scripts/flash.sh
```

`build_release.sh` writes `release/tdeck-plus-<version>-merged.bin` (bootloader, partitions and app; flash at **0x0**) and an app-only `-app.bin` (0x10000, for Launcher/OTA). `flash.sh` flashes the newest merged image at 0x0 and refuses anything else. Writing a merged image at 0x10000 is a known way to brick a T-Deck. Prebuilt images are on the [releases page](https://github.com/John-ODell/tdeck/releases).

**If flashing can't connect** (common when Meshtastic or another firmware is running), enter download mode:
1. Turn the T-Deck off (left-side switch).
2. Hold the trackball down.
3. Turn it on and keep holding for 2–3 s.
4. Release. The screen stays black: that's download mode.
5. Run `scripts/flash.sh`, then press reset or power-cycle.

To name a port: `scripts/flash.sh release/<file>-merged.bin /dev/cu.usbmodemXXXX`.

**Going back to stock Meshtastic:** use [flasher.meshtastic.org](https://flasher.meshtastic.org/) with download mode as above. The LilyGO factory test images are in the [upstream repo](https://github.com/Xinyuan-LilyGO/T-Deck/tree/master/firmware).

The dongle is flashed from ESP-Geek; its README covers a fresh GEEK (BOOT held while plugging in), a running one (`geekflash.py`, keeps the pairing) and the other boards.

## Map tiles

Copy 256 px PNG slippy tiles to the SD card (FAT32 or exFAT) as either:

```
/map/{z}/{x}/{y}.png
/maps/<style>/{z}/{x}/{y}.png
```

This is the same layout Meshtastic MUI uses, so MUI tile packs work as-is. With a GEEK paired, put the same `map/` tree on its card (it is a USB drive on the computer) and press `s` in the Map app to pull it over the air. 8-bit palette PNGs decode fastest. Respect the tile provider's terms; OpenStreetMap's public tile servers forbid bulk downloads.

## Serial console

At 115200 baud, over the USB port, one command per line:

| Command | Does |
|---|---|
| `open <app>` | Opens an app by name, e.g. `open Spectrum` |
| `key <k>` | Injects a key: `enter`, `esc`, `up`, `down`, `left`, `right` or one character |
| `tap <x> <y>` | Presses the screen at that pixel for 80 ms and releases, as a finger would |
| `home` | Back to the launcher |
| `lock` / `unlock` | Lock or unlock the screen (a locked T-Deck ignores `key` and `tap`) |
| `ls <dir>` / `cat <file>` | List a directory / print a file from the SD card (or internal flash without one; `flash:/x` forces flash) |
| `shot` | Screenshot: `SHOT <w> <h>`, base64 RGB565 little-endian lines, then `END` (the active screen; toasts are not in it) |
| `rssi` | SX1262 instantaneous RSSI at several settle times (sweep diagnostics) |

Output only arrives if the host opened the port before the device booted, so reset after connecting. The dongle answers `{"get":"link"}` and `{"get":"screen"}` on its own port.

## Build and test

```bash
pio run
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
src/hal/               display + backlight, keyboard/trackball/touch, battery, GPS, BLE HID
src/mesh/              protocol constants, protobuf codec, crypto, router + radio
src/link/              the dongle link: pairing, encryption, the packet types, the companion relay
src/core/              settings (NVS), node DB and message log (LittleFS), SD, serial console
src/ui/                launcher, status bar, toasts, helpers
src/apps/              one file per app
lib/                   LilyGO's preconfigured TFT_eSPI and SensorLib (don't replace with registry copies)
tools/meshterm.py      the computer as a Meshtastic terminal through the dongle
dongle/                which dongle release goes with which T-Deck build
docs/                  the write-up, screenshots, and the protocol and hardware research
```

Every change is one issue, one pull request, one review and one squash commit.

## License

[GPL-3.0](LICENSE). The firmware links [ESP8266Audio](https://github.com/earlephilhower/ESP8266Audio) (GPL-3.0), so a build of it is GPL either way; the rest of the code is offered under the same terms. LilyGO's TFT_eSPI and SensorLib copies in `lib/` keep their own licenses.

## What is not done

- Meshtastic 2.8 XEdDSA packet signatures are not implemented. Nodes accept unsigned packets by default, and chat with stock nodes (an M5Stack among them) works without them.
- GPS detection is confirmed on the u-blox M10 only; the L76K path is written from its documentation.
- Battery voltage was never checked against a meter (the pack is sealed in the case); the divider ratio comes from the schematic (#5).
- Only the T-Deck Plus has been flashed. The original T-Deck shares the pin map but is untried.
