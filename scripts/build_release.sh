#!/bin/sh
# Build both halves and write, for each, a merged image (flash at 0x0), a checksum file and
# an ESP Web Tools manifest for flashing from a browser; the T-Deck also gets an app-only image.
set -e
cd "$(dirname "$0")/.."
PIO=${PIO:-$(command -v pio || echo "$HOME/.local/bin/pio")}
"$PIO" run -e tdeck-plus
B=.pio/build/tdeck-plus
PKG="$HOME/.platformio/packages"
BOOT_APP0="$PKG/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
VER=$(git describe --tags --always --dirty)
mkdir -p release
"$PIO" pkg exec --package tool-esptoolpy -- esptool.py --chip esp32s3 merge_bin -o "release/tdeck-plus-$VER-merged.bin" \
    --flash_mode keep --flash_freq keep --flash_size 16MB \
    0x0 "$B/bootloader.bin" 0x8000 "$B/partitions.bin" 0xe000 "$BOOT_APP0" 0x10000 "$B/firmware.bin"
cp "$B/firmware.bin" "release/tdeck-plus-$VER-app.bin"
( cd release && shasum -a 256 "tdeck-plus-$VER-merged.bin" "tdeck-plus-$VER-app.bin" > "tdeck-plus-$VER.sha256" )
# ESP Web Tools manifest (#187): a page with <esp-web-install-button manifest="..."> flashes
# the merged image at 0x0 from the browser. Host it next to the image; GitHub release
# downloads do not send CORS headers, so copy both to the site.
cat > "release/tdeck-plus-$VER-manifest.json" <<MANIFEST
{
  "name": "tdeck",
  "version": "$VER",
  "new_install_prompt_erase": true,
  "builds": [
    {
      "chipFamily": "ESP32-S3",
      "parts": [{ "path": "tdeck-plus-$VER-merged.bin", "offset": 0 }]
    }
  ]
}
MANIFEST

# The GEEK companion dongle (dongle/, #192). Its merged image is a factory image: merge_bin
# fills the gap over NVS (0x9000-0xE000) with 0xFF, so a flashed GEEK forgets its pairing
# and comes up in pairing mode. dongle/tools/geekflash.py reflashes a running GEEK and
# keeps the pairing.
( cd dongle && "$PIO" run -e geek )
D=dongle/.pio/build/geek
"$PIO" pkg exec --package tool-esptoolpy -- esptool.py --chip esp32s3 merge_bin -o "release/tdeck-dongle-$VER.bin" \
    --flash_mode keep --flash_freq keep --flash_size 16MB \
    0x0 "$D/bootloader.bin" 0x8000 "$D/partitions.bin" 0xe000 "$BOOT_APP0" 0x10000 "$D/firmware.bin"
( cd release && shasum -a 256 "tdeck-dongle-$VER.bin" > "tdeck-dongle-$VER.sha256" )
cat > "release/tdeck-dongle-$VER-manifest.json" <<MANIFEST
{
  "name": "tdeck dongle (Waveshare ESP32-S3-GEEK)",
  "version": "$VER",
  "new_install_prompt_erase": true,
  "builds": [
    {
      "chipFamily": "ESP32-S3",
      "parts": [{ "path": "tdeck-dongle-$VER.bin", "offset": 0 }]
    }
  ]
}
MANIFEST
echo
echo "release/tdeck-plus-$VER-merged.bin     -> flash at 0x0 (scripts/flash.sh)"
echo "release/tdeck-plus-$VER-app.bin        -> app only, for Launcher / OTA (0x10000)"
echo "release/tdeck-plus-$VER-manifest.json  -> ESP Web Tools manifest, host it beside the merged image"
echo "release/tdeck-dongle-$VER.bin          -> the GEEK, at 0x0 with BOOT held while plugging in (wipes the pairing)"
echo "release/tdeck-dongle-$VER-manifest.json -> ESP Web Tools manifest for the GEEK"
