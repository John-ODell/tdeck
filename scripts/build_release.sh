#!/bin/sh
# Build the firmware and write a merged image (flash at 0x0) plus the app-only image.
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
echo
echo "release/tdeck-plus-$VER-merged.bin   -> flash at 0x0 (scripts/flash.sh)"
echo "release/tdeck-plus-$VER-app.bin      -> app only, for Launcher / OTA (0x10000)"
