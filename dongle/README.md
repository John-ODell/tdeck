# USB keyboard dongle

Firmware for an ESP32-S3 or ESP32-S2 board with native USB. Plugged into a computer it
is a USB keyboard and mouse that types what the T-Deck's **Keyboard** app sends.

```bash
cd dongle
pio run -e s3 -t upload    # ESP32-S3 boards
pio run -e s2 -t upload    # ESP32-S2 boards (S2 Mini etc.)
```

If the board doesn't show up for flashing, hold BOOT while plugging it in.

## Pairing

1. Flash and plug in the dongle. Unpaired, its LED blinks fast.
2. On the T-Deck: **Keyboard** › Send: **USB dongle** › **Pair a dongle**, pick it.
3. The LED goes solid while the T-Deck is in range. Start typing.

To pair it with another T-Deck, hold BOOT for 2 s. The old pairing stays until a new one
is confirmed, and pairing mode gives up after 2 minutes if one already exists.

| LED | Meaning |
| --- | --- |
| Fast blink | Pairing mode |
| Slow blink | Paired, T-Deck not heard for 3 s |
| On | Connected |

The LED pin is set per env in `platformio.ini` (`LED_RGB_PIN=48` for the S3 DevKitC
RGB LED, `LED_PIN=15` for the S2 Mini). Change it for other boards.

## How it talks

ESP-NOW on WiFi channel 1. Pairing is an X25519 exchange; both ends derive the ESP-NOW
link key from it, so every packet after that is AES-CCM encrypted, and each also carries
a truncated HMAC that the dongle checks. Keys are sent as press-and-release taps so a
lost packet can't leave a key stuck down. The protocol is in
[`../src/link/link_proto.h`](../src/link/link_proto.h), shared by both sides.
