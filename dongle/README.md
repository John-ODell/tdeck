# The GEEK companion dongle

The firmware for the USB dongle half of this project: a Waveshare ESP32-S3-GEEK that plugs
into a computer and is the T-Deck's keyboard, mouse, mesh terminal and shared drive, and
locks the computer when the T-Deck goes away. It is a PlatformIO project of its own in this
folder, covered by the repo's [GPL-3.0 license](../LICENSE).

It began as `firmware/tdeck-dongle` in John-ODell/ESP-Geek (a private lab for other GEEK
firmwares) and was brought here at that repo's `main` 2808d40 (2026-10-26) so the public
repo builds both halves on its own. The link protocol is `src/link/link_proto.h` at the
repo root, included directly, so there is no copy to keep in step.

## What is here

```
platformio.ini     envs: geek (the Waveshare ESP32-S3-GEEK), s3 and s2 (bare boards)
src/main.cpp       the dongle: pairing, the encrypted ESP-NOW link, USB keyboard and mouse,
                   the serial JSON port, the screen, the card as a USB drive, lock-when-away
lib/Geek/          board support: pins, USB identity, the screen as a sprite, the card
tools/geekflash.py reflash a running GEEK without touching it, keeping the pairing
tools/geekshot.py  save the GEEK's screen as a PNG ({"get":"screen"})
```

TFT_eSPI comes from `../lib`, the same copy the T-Deck uses.

| Env | Board | Gets |
| --- | --- | --- |
| `geek` | Waveshare ESP32-S3-GEEK | everything: keyboard, mouse, mesh terminal, the TF card as a shared drive, link status and the last keys on its screen, lock-when-away |
| `s3` | any ESP32-S3 board with USB on the native pins | keyboard, mouse, mesh terminal, lock-when-away; no drive, no screen |
| `s2` | any ESP32-S2 board | the same as `s3` |

## Build and flash

```bash
cd dongle && pio run -e geek
```

A GEEK that has never run this (or is wedged): hold **BOOT** while plugging it in, then

```bash
cd dongle && pio run -e geek -t upload
```

A GEEK already running the dongle reflashes without touching it, and keeps its pairing:

```bash
~/.local/share/uv/tools/platformio/bin/python dongle/tools/geekflash.py /dev/cu.usbmodemXXXX dongle/.pio/build/geek
```

The firmware starts USB from `setup()` with its own name and product id, so esptool's
DTR/RTS reset does not reach the chip. The ESP32-S3 ROM does watch the CDC line state,
though: the sequence dtr/rts = 0/1, 1/1, 1/0, 0/0 reboots it into the bootloader, which
shows up as a new `usbmodem` port. `geekflash.py` does that, then runs esptool 5.x
(`uvx esptool`) with `--before no-reset --after watchdog-reset` to write the bootloader,
partition table, `boot_app0` and the firmware. It needs a Python with pyserial;
PlatformIO's own works. Eject the GEEK's card from the computer first, or the OS complains
about a disk that was not ejected.

## Release images

`scripts/build_release.sh` at the repo root builds both halves and writes
`release/tdeck-dongle-<version>.bin` (a merged image for 0x0), its `.sha256` and an ESP
Web Tools `-manifest.json`, next to the T-Deck's. The merged image is a factory image: the
gap over NVS (0x9000 to 0xE000) is 0xFF, so the GEEK forgets its pairing and the replay
counter and comes up in pairing mode. Flash it with the GEEK held in the bootloader:

```bash
uvx esptool --chip esp32s3 write-flash 0x0 tdeck-dongle-<version>.bin
```

## Pairing

With the GEEK's screen saying **Pairing** (hold BOOT for 2 s to get there from a paired
one), open **Keyboard** on the T-Deck, set Send to **USB dongle** and press **Pair**. The
screen says **Connected**, and `{"get":"link"}` on the GEEK's serial port answers
`"state":"online"`.

## Serial port

115200 baud, JSON lines. `{"get":"link"}` reports the link, `{"get":"screen"}` returns the
240×135 framebuffer as base64 RGB565 (`tools/geekshot.py PORT out.png [scale]` saves it),
`{"set":"lock","after":N}` sets lock-when-away from the computer side. With **Mesh chat: To
dongle** on, every chat message arrives as a JSON line and a line sent to the port goes out
on the mesh; `tools/meshterm.py` at the repo root wraps that.

## Which release goes with which

| T-Deck | `LINK_VERSION` | Dongle |
| --- | --- | --- |
| [`v0.2.0`](https://github.com/John-ODell/tdeck/releases/tag/v0.2.0) and later | 2 | the `tdeck-dongle-<version>.bin` on the same release |
| `main` from `cba755f` (2026-10-25) to `v0.2.0` | 2 | ESP-Geek `tdeck-dongle-v1.0.0` (no lock-when-away) |
