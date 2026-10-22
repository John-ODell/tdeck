#!/bin/sh
# Flash the newest merged image at 0x0. Usage: scripts/flash.sh [image.bin] [port]
# Put the T-Deck in download mode first if a normal flash fails: power off, hold the
# trackball down, power on, release after 2-3 s (screen stays black).
set -e
cd "$(dirname "$0")/.."
IMG=${1:-$(ls -t release/*-merged.bin 2>/dev/null | head -1)}
[ -n "$IMG" ] && [ -f "$IMG" ] || { echo "No merged image. Run scripts/build_release.sh first."; exit 1; }
case "$IMG" in *-merged.bin) ;; *) echo "Refusing: $IMG is not a merged image (those go at 0x0)."; exit 1 ;; esac
PIO=${PIO:-$(command -v pio || echo "$HOME/.local/bin/pio")}
PORT_ARG=""
[ -n "$2" ] && PORT_ARG="--port $2"
echo "Flashing $IMG at 0x0"
"$PIO" pkg exec --package tool-esptoolpy -- esptool.py --chip esp32s3 $PORT_ARG --baud 921600 --before default_reset --after hard_reset \
    write_flash -z 0x0 "$IMG"
echo "Done. If the screen stays dark, press the reset button (left side)."
