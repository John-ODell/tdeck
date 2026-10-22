#!/bin/sh
# Boot the newest merged image in Espressif QEMU for 45 s; fail on any panic.
# Usage: scripts/qemu_smoke.sh /path/to/qemu-system-xtensa
# (from https://github.com/espressif/qemu/releases, check its sha256 file)
set -e
QEMU=${1:?path to qemu-system-xtensa}
cd "$(dirname "$0")/.."
IMG=$(ls -t release/*-merged.bin | head -1)
out=$(mktemp -d)
# Pad to 16 MB with 0xFF (erased flash)
python3 -I -c "import sys; d=open(sys.argv[1],'rb').read(); open(sys.argv[2],'wb').write(d+b'\xff'*(16*1024*1024-len(d)))" "$IMG" "$out/flash.bin"
"$QEMU" -nographic -machine esp32s3 -m 8M -drive file="$out/flash.bin",if=mtd,format=raw -no-reboot > "$out/boot.log" 2>&1 &
pid=$!
sleep 45
kill $pid 2>/dev/null || true
if grep -q -E 'Guru Meditation|abort\(\)|Rebooting' "$out/boot.log"; then
    cat "$out/boot.log"
    echo "QEMU smoke: PANIC ($IMG)"
    exit 1
fi
echo "QEMU smoke: no panic in 45 s ($IMG)"
