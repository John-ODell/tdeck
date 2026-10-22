# T-Deck / T-Deck Plus Alternative Firmware Landscape (as of Oct 2026)

Status: complete within a ~15-call research budget. Remaining gaps are listed per section.

## Which firmwares exist, what they do, maturity, complaints

### Takeaway
There are several firmware choices. Meshtastic with the LVGL-based MUI (device-ui) is the official touch UI for T-Deck/T-Deck Plus. MeshCore "Ultra" is the closed-source standalone MeshCore UI, with some features locked behind a paid key. The rest of the field is security tools (Bruce), launchers (bmorcelli Launcher), an OS (Tactility) and single-purpose apps (acid-drop IRC, Blackout Comms, WADAMESH).

### Cited Findings
- **Meshtastic MUI / device-ui**: a C++ LVGL UI library (MVC design, GPL-3.0). It is built with CMake and also has a PlatformIO `library.json` manifest. It supports T-Deck/T-Deck Plus, T-Lora Pager, T-Watch S3, SenseCAP Indicator and others. Features include a nodes panel (sort/filter/telemetry), group channels, chat with ACKs, a map with pan/zoom, and settings (region, preset, role, brightness, language). Tiles load from SD or WLAN, and PMTiles is listed. Open items include E-Ink, some localization, and Latin/Cyrillic glyph work. The CYD board was dropped for insufficient memory — [meshtastic/device-ui](https://github.com/meshtastic/device-ui)
- The Meshtastic 2.8 docs list MUI as the supported UI for T-Deck/T-Deck Plus. Base UI keyboard shortcuts (alt+c combos, e.g. alt+c then g to toggle GPS) are unavailable under MUI. Only alt+b (backlight) works in both — [meshtastic.org T-Deck page](https://meshtastic.org/docs/hardware/devices/lilygo/tdeck/)
- MUI limitation: the Client API allows only one connection, so a phone app cannot connect while MUI runs. You must boot into BaseUI or BT programming mode. Some advanced settings still need a phone client — [Meshtastic UI docs](https://meshtastic.org/docs/configuration/device-uis/meshtasticui/)
- MUI navigation is touch-centric (tap/long press). The docs page does not describe keyboard navigation — [Meshtastic UI docs](https://meshtastic.org/docs/configuration/device-uis/meshtasticui/)
- **MeshCore Ultra**: the T-Deck firmware is developed by Scott at Ripple Radios, the MeshCore creator. Some advanced but optional features need a registered unlock key — [MeshCore FAQ](https://docs.meshcore.io/faq/?h=)
- Ultra v6.0 introduced multi-network profiles (Ripple and MeshCore) — [Ripple "Some Big News"](https://buymeacoffee.com/ripplebiz/some-big-news?l=en)
- Ultra v7.14 (Aug 2025, older info) covers T-Deck, T-Deck Pro, T-Pager, T-Watch and T-Display Pro. It uses touch-quadrant plus trackball navigation and runs a first-boot help wizard — [Ultra v7.14 guide](https://buymeacoffee.com/ripplebiz/ultra-v7-7-guide-meshcore-users)
- **Meck**: a community MeshCore fork (MeshCore v1.11 base) for T-Deck Pro / T-Deck Max. It adds repeater admin, traceroute, private channels and games. It wants a 32 GB or larger SD card, and without one most extended features "fail silently" — [Meck wiki](https://github.com/pelgraine/Meck/wiki)
- **WADAMESH**: a touch-UI MeshCore firmware for T-Deck Plus. A YouTube reviewer says it revived a device they had nearly shelved — [YouTube](https://www.youtube.com/watch?v=e63uKp9r65U)
- **Blackout Comms**: a "secure mesh" communicator firmware with a T-Deck Plus build. v3.7.3 is dated 8/29/2026, with US and non-US prebuilt bins — [offgridcomms.club](https://www.offgridcomms.club/firmware/esp32/tdeck_firmware.html)
- **acid-drop**: an IRC client firmware for T-Deck / T-Deck Plus (direct IRC, ZNC, WeeChat relay), built with PlatformIO — [acidvegas/acid-drop](https://github.com/acidvegas/acid-drop)
- **Bruce**: "Predatory ESP32 firmware" (pentest tools). Its device table shows T-Deck with CC1101 and BadUSB working, and NRF24, FM, PN532 and speaker unavailable. It uses bmorcelli's Launcher — [BruceDevices/firmware](https://github.com/BruceDevices/firmware)
- **Launcher (bmorcelli)**: a multi-app launcher for ESP32 boards, ported to T-Deck. It added WASD-style navigation on T-Deck/T-Deck Plus. It copies data partitions as-is, which makes it compatible with UiFlow, Tactility and others — [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher)
- **Stock LilyGO**: the vendor repo ships factory/UnitTest examples and a TouchFix firmware for touch problems on rounded-screen T-Deck Plus units — [Xinyuan-LilyGO/T-Deck](https://github.com/Xinyuan-LilyGO/T-Deck)
- An OM7TEK 2025 guide (older info) covers an experimental Meshtastic "Fancy UI" build for T-Deck Plus and notes a taken-down download version — [OM7TEK](https://www.om7tek.com/2025/how-to-flash-and-setup-meshtastic-with-ui-on-t-deck-plus/)

- **Tactility**: an ESP32-family OS with a GUI. Native C/C++ apps load from SD, and it ships Launcher, Settings, Terminal and Files apps. It is GPL-3.0, with CMake/ESP-IDF-style partition CSVs (with-sd / no-sd). The repo shows ~907 stars and ~653 commits. The README does not explicitly list T-Deck. Launcher's changelog claims Tactility compatibility, which suggests some T-Deck use — [TactilityProject/Tactility](https://github.com/TactilityProject/Tactility); [Hackster](https://www.hackster.io/kenvanhoeylandt/tactility-857d4b); [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher)
- **History of Meshtastic's T-Deck UI (older info)**: the "fancy UI" originally came from the Meshtastic `tft` branch (`pio run -e t-deck-tft`) and had to be self-built until MUI previews shipped — [Lemmy thread](https://lemmy.cadence.moe/mander.xyz/p/24441769); [Meshtastic Discourse](https://meshtastic.discourse.group/t/t-deck-ui-firmware/13795)
- **Other T-Deck-family firmwares**: Fennek (GPL-3.0, T-Deck Pro; music, audiobooks, eBooks, MeshCore chat) — [awesome.ecosyste.ms](https://awesome.ecosyste.ms/projects/github.com%2Fdanst0%2Ffennek). Aurora, a community MeshCore-style T-Deck firmware at v0.0.5-alpha — [lemmy](https://lemmy.cadence.moe/mander.xyz/p/24441768). T-DeckARD (CircuitPython 10.x) — [luxferre](https://code.luxferre.top/luxferre/t-deckard/raw/commit/97de31f2ada60865eb53d5dae42bf478d9ee3c35/README.md). espp t-deck BSP component — [Espressif components](https://components.espressif.com/components/espp/t-deck/versions/1.3.1/readme)
- **"T-Deck-Pager"**: no repository with this name was found. It may mean the LilyGO T-Lora Pager, a separate device supported by MUI and MeshCore Ultra — [device-ui](https://github.com/meshtastic/device-ui); [MeshCore FAQ](https://docs.meshcore.io/faq/?h=)

### Inferences
- No firmware combines Meshtastic protocol compatibility, a keyboard-first UI, offline maps, WiFi/BLE tools and SD notes. MUI is touch-first and mesh-only. Bruce/Launcher have the tools but no mesh. MeshCore Ultra is polished but not Meshtastic-compatible and partly paywalled.
- A firmware that is installable from Launcher (standard app partition layout) would let users dual-boot with Bruce/MUI. That would lower the switching cost.

### Gaps
- Tactility's T-Deck device support is not confirmed from its own device list.
- Reddit threads were not reachable through search. Complaints come from GitHub issues and reviews instead.

## What owners want that none do well

### Takeaway
Recurring pain points in 2025–2026:
- MUI stability (hangs, failure to wake, crashes)
- Notification controls that do not work
- BaseUI sluggishness and broken free-text keyboard
- WiFi/BT working fully only in BaseUI
- Backlight-driven battery drain
- No true fuel gauge
- MUI locking out the phone app

### Cited Findings
- July 2026 device-ui issue: Ping-channel notifications still fire after updating to 2.7.26 even with notifications disabled, and the T-Deck sometimes will not wake after a few idle hours — [device-ui #339](https://github.com/meshtastic/device-ui/issues/339)
- Oct 2026: MUI 2.8.1 crashes on long-press of the Bluetooth icon. The maintainer says it is fixed in PR 12159 — [device-ui #436](https://github.com/meshtastic/device-ui/issues/436)
- Sep 2026: BaseUI on T-Deck Plus is sluggish (multi-second touch latency), and the keyboard does not work for free-text mode — [firmware #11921](https://github.com/meshtastic/firmware/issues/11921)
- A user says many choose BaseUI over MUI because only there do WiFi and BT fully work. They asked BaseUI to use the internal keyboard — [firmware #9018](https://github.com/meshtastic/firmware/issues/9018)
- Community opinion: the T-Deck's big screen and keyboard mostly drain the battery faster. One user on an older MUI build reports the backlight seems always on — [Meshtastic Discussion #308](https://github.com/orgs/meshtastic/discussions/308) (anecdotal)
- A reviewer reports LilyGO "has openly admitted" the T-Deck lacks true battery measurement and only estimates charge. They recommend charging below about 30% — [The Secure Dad review](https://www.thesecuredad.com/post/lilygo-t-deck-plus-review-standalone-meshtastic-without-your-phone)
- A buyer complaint says the battery should last more than 24 h with modest use — [eBay listing review](https://www.ebay.de/itm/376426143166)
- MUI loses Base UI keyboard shortcuts — [meshtastic.org](https://meshtastic.org/docs/hardware/devices/lilygo/tdeck/)

### Inferences
- Opportunities: full keyboard and trackball navigation with hotkeys, a calibrated battery gauge (voltage curve plus coulomb estimate), coexistence with the phone app over BLE, and power profiles.

### Gaps
- Systematic Reddit/GitHub-issue mining is still pending.

## Offline maps: MUI tile format and reuse

### Takeaway
MUI uses plain raster slippy tiles: 256x256 PNG (8-bit palette recommended) on SD at `/maps/<style>/{z}/{x}/{y}.png`, or `/map/` for a single style. A small firmware can reuse exactly this layout and stay drop-in compatible with existing MUI tile packs.

### Cited Findings
- Single style: `/map`. Multiple styles: `/maps/{STYLE}/`. Use MBR-formatted SD, FAT32 or exFAT (exFAT recommended). Tiles are 256x256 PNG, with a 256-color 8-bit palette recommended. The starter set is zoom 1–6 for the whole Earth. The default view with no positions is Big Ben, London — [Meshtastic UI docs](https://meshtastic.org/docs/configuration/device-uis/meshtasticui/)
- The maps README describes the structure `/maps/<style>/z/x/y`. Downloadable zooms run 1–20. Starter styles come from Thunderforest (atlas, outdoors), Geoapify (positron, dark-matter-brown) and OSM. Users can supply a raster URL template for WiFi download. Tools named: QGIS, MapTiler (mbtiles), go-pmtiles, VersaTiles, Oxed's Map Tile Downloader (unaffiliated) and the Geofabrik Tile Calculator. Pack sizes: USA z1–12 about 1.4 GB, France Outdoors z1–13 about 3.9 GB, Netherlands about 956 MB — [device-ui/maps](https://github.com/meshtastic/device-ui/tree/master/maps)
- device-ui lists PMTiles support and SD/WLAN dynamic loading — [meshtastic/device-ui](https://github.com/meshtastic/device-ui)

### Inferences
- Recommended for the new firmware: read `/maps/<style>/{z}/{x}/{y}.png` (MUI-compatible), and optionally JPEG for photo or satellite layers. LVGL can decode PNG via lodepng/libpng, and the ESP32-S3 with PSRAM can cache a 3x3 tile window (about 9 x 128 KB at RGB565).
- Tile provider terms (Thunderforest/Geoapify require API keys, and the OSM tile policy forbids bulk download) matter if the firmware downloads tiles over WiFi.

### Gaps
- PNG decode speed on ESP32-S3 has not been measured.

## Battery life and power saving

### Takeaway
(partial) The T-Deck Plus has a 2000 mAh cell. Real-world Meshtastic use is about 12 h of medium use per one reviewer. There is no fuel gauge, only a voltage estimate.

### Cited Findings
- 2000 mAh battery on T-Deck Plus — [LilyGO product page](https://lilygo.cc/products/t-deck-plus-meshtastic); [meshtastic.org](https://meshtastic.org/docs/hardware/devices/lilygo/tdeck/)
- About 12 h of "medium use" (messaging, nodes, occasional maps) — [The Secure Dad review](https://www.thesecuredad.com/post/lilygo-t-deck-plus-review-standalone-meshtastic-without-your-phone)
- Backlight can be toggled with alt+b — [meshtastic.org](https://meshtastic.org/docs/hardware/devices/lilygo/tdeck/)

### Inferences
- The biggest levers are likely backlight PWM and timeout, keyboard backlight off, CPU at 80 MHz when idle with light-sleep between LoRa IRQs, GPS in periodic or backup mode, and WiFi/BLE off by default. These are unmeasured inferences.

### Gaps
- No measured current-draw figures yet (pending).

## Flashing

### Takeaway
Meshtastic uses its Web Flasher with a "1200bps reset". The manual fallback is: power off, hold the trackball, power on, and release after 2–3 s. A black screen with backlight off means download mode. Merged images go to 0x0. The ESP32-S3 native USB-CDC/JTAG serves both flashing and console.

### Cited Findings
- Web Flasher "T Deck" target with a 1200bps reset. The manual trackball-hold procedure is as above. Do not transmit without an antenna. Filenames follow `firmware-t-deck-X.X.X.xxxxxxx.bin` — [meshtastic.org](https://meshtastic.org/docs/hardware/devices/lilygo/tdeck/)
- Merged images (from `esptool merge-bin`) are written at 0x0. A merged image misdetected as app-only and written at 0x10000 bricked the layout — [PlatformIO forum](https://community.platformio.org/t/how-to-use-esptool-exe-to-upload-application-firmware-bin-and-spiffs-bin/54003); [RTNode flash.py](https://git.rns.moscow/deuxyeux/RTNode-HeltecV4/src/commit/760e92f186fb1467b5e2f8dbf13d568c31979a60/flash.py)
- Offsets depend on the SDK. Consult the SDK docs — [esptool docs](https://docs.espressif.com/projects/esptool/en/latest/esptool/basic-commands.html)

### Inferences
- A custom app needs nothing special beyond download mode. Ship a merged bin for 0x0 plus an app-only bin (0x10000 on the default Arduino layout) for Launcher/OTA use.

### Gaps
- Exact LilyGO partition table for T-Deck (16 MB flash) not yet confirmed (pending).

## Licensing

### Takeaway
Meshtastic firmware and device-ui are GPL-3.0, so copying their code makes the new firmware GPL-3.0. LVGL and RadioLib are MIT. Implementing the Meshtastic protobuf/radio protocol independently avoids GPL obligations only if no GPL code is copied.

### Cited Findings
- device-ui is GPL-3.0 — [meshtastic/device-ui](https://github.com/meshtastic/device-ui)

### Gaps
- License files for meshtastic/firmware, the protobufs repo, LVGL and RadioLib have not been directly confirmed (pending).
