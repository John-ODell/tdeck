# The GEEK and the T-Deck together

What the two boards bring, and what pairing them up could do beyond the keyboard dongle.
The ESP-NOW link in `src/link/` (pairing, encryption, tags) is the plumbing for all of
it: each idea is a new packet type on that link plus a little code at each end.

## What each board has

| | T-Deck Plus | ESP32-S3-GEEK |
| --- | --- | --- |
| Radio | LoRa 915 MHz, WiFi, BLE | WiFi, BLE |
| Input | Keyboard, trackball, touch | BOOT button |
| Screen | 2.8" 320×240 | 1.14" 240×135 |
| Storage | microSD | microSD (TF) |
| Host link | USB-C (CDC) | **USB-A plug**: keyboard, mouse, serial or disk to the computer |
| Headers | Grove (wired to the GPS) | I2C, UART, 1 GPIO/ADC |
| Position | GPS | none |

The GEEK's trick is the USB-A plug: it can be any USB device class TinyUSB offers, and it
sits on a desk powered all day. The T-Deck's trick is the LoRa radio and the keyboard. So
the useful shapes are **the T-Deck reaching the computer through the GEEK**, and **the
computer reaching the mesh through the T-Deck**.

## Ideas, most useful first

### 1. Mesh messages on the desk

The T-Deck forwards every chat message and node event over the link; the GEEK shows the
latest on its screen and, over USB serial, prints them as JSON lines. A tiny script on the
Mac can turn those into notifications, log them or pipe them to anything.

- Reverse direction too: type a reply on the Mac, the GEEK sends it, the T-Deck
  transmits it on LoRa. The Mac becomes a Meshtastic terminal with no app.
- Needs: `LINK_TEXT` packets both ways; a "Companion" toggle in the Keyboard app or a
  new Mesh Bridge app; the GEEK already has USB CDC.
- Size: a day. The biggest piece is a reliable fragment scheme, since ESP-NOW packets
  hold 250 bytes and a message with its node name can exceed that.

### 2. Notes and logs land on the computer as a disk

The GEEK presents its TF card as a USB drive (TinyUSB mass storage). The T-Deck pushes
`/notes/` and any logs it keeps across the link; the GEEK writes them to the card. Plug
in the GEEK, and the files are a Finder window away. No cable to the T-Deck, no app.

- Also works the other way: drop a text file or map tiles on the GEEK's drive, the T-Deck
  pulls them. A map tile pull would be slow over ESP-NOW (hundreds of KB/s at best) but
  fine for notes and small files.
- Needs: TinyUSB MSC on the GEEK with the SD as the backing store, a file transfer packet
  type with acks, and a Sync button in Notes/Files.
- Size: two days. MSC and SD sharing the card between USB and the firmware is the fiddly
  part (the Mac must see it as removed while the GEEK writes).

### 3. Media keys and a presentation clicker

Cheap extension of the keyboard. HID consumer control (volume, play/pause, next track,
brightness) is a third report ID on both the BLE and USB sides, and the T-Deck's
trackball in "clicker" mode sends Page Down / Page Up for slides.

- Size: half a day. Adds a "Media" row to the Keyboard app.

### 4. Second set of eyes for Spectrum

The GEEK is a second WiFi/BLE receiver. In the Spectrum app it could scan the channels
the T-Deck isn't on, or sit across the room as a fixed probe, and stream its hits over
the link so the T-Deck's waterfall shows both. One board sweeping WiFi while the other
watches BLE doubles the picture.

- The catch: ESP-NOW needs the GEEK's radio on channel 1, and scanning takes it off that
  channel. Hits would be sent in bursts between hops, and the link would drop out during
  the sweep. Workable, but the GEEK would be a "sampler" rather than always on.
- Size: a day, mostly on the T-Deck side merging two sources.

### 5. The GEEK as the computer's mesh radio

Today Meshtastic's desktop and web clients talk to a node over USB serial using the
Meshtastic protobuf framing. If the GEEK speaks that framing on its USB port and relays
packets to the T-Deck over the link, the Meshtastic web client and the `meshtastic` CLI
would see the T-Deck as a plugged-in node while it stays in a pocket.

- This is the ambitious one. It needs the admin, config and node-DB protobufs to be
  answered convincingly, which this firmware doesn't implement (it only does chat, nodeinfo
  and position). A partial version that only passes text messages and node lists is more
  realistic and still useful with the CLI.
- Size: a week for the partial version.

### 6. Wake, find and locate

Small things that fall out of the link once the GEEK is on the desk all day:

- **Find my T-Deck**: press the GEEK's BOOT button, the T-Deck beeps and lights up if in
  range. The T-Deck has no speaker, so it's a screen flash and a status line.
- **Last seen**: the GEEK's screen shows how long since the T-Deck was heard, and the
  T-Deck's last GPS position as a line, so it says where it was when it left.
- **Clock**: the T-Deck sets its clock from GPS; the GEEK could set its own from the
  T-Deck and show it when idle.
- Size: an hour each.

### 7. A second GEEK

John has two. The second can be an ESP-NOW repeater (the link protocol would need a hop
field) to reach a T-Deck two rooms away, or an always-on bridge from a Raspberry Pi: the
Pi runs the scripts from idea 1 and 2 instead of the Mac, so the desk doesn't need the
Mac awake.

## What I would build first

1 and 3. Idea 3 is small and makes the keyboard dongle more useful today. Idea 1 is the
one that uses what's unique about each board: the Mac gets a mesh terminal with no
software, and it shares most of its code with 2 later (the fragment and ack scheme).

## Notes on the hardware

- GEEK pins (Waveshare wiki and the espp component): LCD ST7789 240×135 on CS 10, MOSI
  11, SCLK 12, DC 8, RST 9, backlight 7; TF card D0 37, D1 33, D2 38, D3/CS 34, CLK 36,
  CMD 35; BOOT on GPIO 0; UART header TX 43, RX 44; ADC on GPIO 6. The I2C header pins are
  in the wiki's diagram only, not its text, so they're unverified here.
- No LED on the GEEK. The T-Dongle S3 has an APA102 LED and a 0.96" screen on different
  pins; it runs the plain `s3` build with no status light.
- ESP-NOW throughput is tens of KB/s in practice; fine for messages, notes and HID, not
  for anything bulk.
