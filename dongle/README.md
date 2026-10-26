# USB dongle

The dongle firmware lives in [John-ODell/ESP-Geek](https://github.com/John-ODell/ESP-Geek)
as [`firmware/tdeck-dongle`](https://github.com/John-ODell/ESP-Geek/tree/main/firmware/tdeck-dongle),
with the Waveshare ESP32-S3-GEEK's board support, `geekflash.py` and `geekshot.py` under
`common/`. It moved there on 2026-10-25 so every firmware for that board builds from one
place. `tools/meshterm.py` stays here.

## Which release

The T-Deck owns the link protocol: [`src/link/link_proto.h`](../src/link/link_proto.h) is
the original and ESP-Geek keeps a copy that its `tools/sync-proto.sh` refreshes from this
checkout. Both ends must be built from the same `LINK_VERSION`; a mismatch is logged on the
dongle's serial port and shown on the Keyboard page.

| T-Deck | `LINK_VERSION` | Dongle release |
| --- | --- | --- |
| `main` from `cba755f` (2026-10-25) on | 2 | [`tdeck-dongle-v1.0.0`](https://github.com/John-ODell/ESP-Geek/releases/tag/tdeck-dongle-v1.0.0) or later |

After changing `link_proto.h` here: bump `LINK_VERSION`, run `sync-proto.sh` in ESP-Geek,
rebuild and reflash the dongle, and add a row.

## Flashing and pairing

- The release's **factory image** (`tdeck-dongle-vX.Y.Z.bin`, written at 0x0 with the GEEK
  held in the bootloader) clears NVS, so the dongle forgets its pairing and comes up in
  pairing mode.
- `common/tools/geekflash.py` in ESP-Geek reflashes a running dongle without touching it
  and writes only the four firmware regions, so the pairing survives.

To pair again: with the dongle's screen saying "Pairing" (hold BOOT for 2 s from a paired
one), open **Keyboard**, set Send to **USB dongle**, choose **Pair a dongle** and pick it.
