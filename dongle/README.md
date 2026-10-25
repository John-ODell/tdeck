# USB keyboard dongle

Firmware for an ESP32-S3 or ESP32-S2 board with native USB. Plugged into a computer it
is a USB keyboard and mouse that types what the T-Deck's **Keyboard** app sends.

```bash
cd dongle
pio run -e geek -t upload  # Waveshare ESP32-S3-GEEK (status on its screen)
pio run -e s3 -t upload    # other ESP32-S3 boards (LilyGO T-Dongle S3, DevKitC...)
pio run -e s2 -t upload    # ESP32-S2 boards (S2 Mini etc.)
```

If the board doesn't show up for flashing, hold BOOT while plugging it in. A GEEK already
running this firmware can be reflashed without touching it:

```bash
pio run -e geek && python3 tools/geekflash.py /dev/cu.usbmodemXXXX   # needs pyserial and esptool 5
```

The script reboots it into the bootloader over the CDC line state, writes the images and
restarts it. On the computer the dongle shows up as "T-Deck Keyboard Dongle" (USB
0x303a:0x81de), with its TF card as a drive.

## Pairing

1. Flash and plug in the dongle. Unpaired, its LED blinks fast.
2. On the T-Deck: **Keyboard** › Send: **USB dongle** › **Pair a dongle**, pick it.
3. The LED goes solid while the T-Deck is in range. Start typing.

To pair it with another T-Deck, hold BOOT for 2 s. The old pairing stays until a new one
is confirmed, and pairing mode gives up after 2 minutes if one already exists.

On the GEEK the screen says which of these it is, shows the paired T-Deck's address, and
while connected counts keys and shows the last few typed.

| LED | Meaning |
| --- | --- |
| Fast blink | Pairing mode |
| Slow blink | Paired, T-Deck not heard for 3 s |
| On | Connected |

The LED pin is set per env in `platformio.ini` (`LED_RGB_PIN=48` for the S3 DevKitC
RGB LED, `LED_PIN=15` for the S2 Mini). Change it for other boards.

## Mesh terminal

With **Mesh: To dongle** on in the T-Deck's Keyboard app, every chat message the T-Deck
receives or sends is printed on the dongle's USB serial port as one JSON line:

```
{"type":"text","from":"6ee1a2b3","to":"ffffffff","id":123,"time":1792797772,"status":0,"dm":false,"name":"Garage","text":"hello"}
```

Send a line to the port and the T-Deck transmits it on the channel; `{"send":"hi","to":"6ee1a2b3"}`
makes it a DM. `tools/meshterm.py` in the main repo does this from a terminal. The relay
keeps the T-Deck's WiFi on, so leave it off when running on battery for long. Messages
that arrive while the dongle is out of range stay on the T-Deck only.

## The TF card as a shared drive (GEEK)

Put a FAT-formatted microSD in the GEEK before plugging it in and it shows up on the
computer as a USB drive. The T-Deck reaches the same card over the link:

- **Files → Dongle card** browses it, opens text files, and `g` pulls any file to
  `/dongle/` on the T-Deck (its SD card, or internal flash without one).
- `p` on a local file in **Files** pushes it to `tdeck/` on the card.
- **Sync notes to dongle** in **Keyboard** copies the notes to `tdeck/notes/`.

Drop a file on the card from the computer and read it on the T-Deck across the room.
Reads happen while the computer still has the drive. While the T-Deck writes, the drive
disappears from the computer and the dongle re-plugs itself afterwards, so the computer
re-reads the card; the Mac may mention a disk that wasn't ejected. Don't leave files open
on it during a push. Names starting with a dot (the Mac's `._` shadows and Spotlight
index) are hidden. The link moves tens of KB/s: fine for text and configs, slow for map
tiles.

## Screen capture (GEEK)

`{"get":"screen"}` on the dongle's serial port answers with its 240×135 framebuffer as
base64 RGB565. `tools/geekshot.py PORT out.png [scale]` saves it as a PNG:

```bash
python3 dongle/tools/geekshot.py /dev/cu.usbmodemXXXX geek.png 2
```

## How it talks

ESP-NOW on WiFi channel 1. Pairing is an X25519 exchange; both ends derive the ESP-NOW
link key from it, so every packet after that is AES-CCM encrypted, and each also carries
a truncated HMAC that the dongle checks. Keys, mouse reports and pings also carry a
counter under that HMAC, and the dongle drops anything it has already seen, so a recorded
packet cannot be replayed. Keys are sent as press-and-release taps so a lost packet can't
leave a key stuck down. The T-Deck and the dongle must be flashed from the same tree: a
version mismatch is logged on the dongle's serial port and shown on the T-Deck's Keyboard
page instead of silently typing nothing. The protocol is in
[`../src/link/link_proto.h`](../src/link/link_proto.h), shared by both sides.
