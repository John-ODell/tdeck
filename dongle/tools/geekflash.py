#!/usr/bin/env python3
"""Flash the GEEK dongle from the running firmware, no buttons.

    geekflash.py /dev/cu.usbmodemXXXX [path/to/.pio/build/geek]

The dongle's TinyUSB serial port can't be reset by esptool's DTR/RTS dance, but the
ESP32-S3 ROM watches the CDC line state: the sequence dtr/rts = 0/1, 1/1, 1/0, 0/0 reboots
it into the bootloader, which shows up as a new usbmodem port. esptool (5.x) then writes
the four images with --before no_reset and leaves with --after watchdog_reset, which
restarts the chip into the new firmware. Run it with a Python that has pyserial, e.g.
~/.local/share/uv/tools/platformio/bin/python.
"""
import glob, os, subprocess, sys, time
import serial

port = sys.argv[1]
build = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), "..", ".pio", "build", "geek")
boot_app0 = os.path.expanduser("~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin")

before = set(glob.glob("/dev/cu.usbmodem*"))
s = serial.Serial(port, 115200)
for dtr, rts in ((0, 1), (1, 1), (1, 0), (0, 0)):
    s.dtr, s.rts = bool(dtr), bool(rts)
    time.sleep(0.1)
try:
    s.close()
except Exception:
    pass

deadline = time.time() + 15
newport = None
while time.time() < deadline and not newport:
    time.sleep(0.5)
    now = set(glob.glob("/dev/cu.usbmodem*"))
    new = now - before
    if new:
        newport = sorted(new)[0]
if not newport:
    sys.exit("bootloader port did not appear")
print("bootloader on", newport)
time.sleep(1)

cmd = ["uvx", "esptool", "--chip", "esp32s3", "--port", newport, "--baud", "921600",
       "--before", "no_reset", "--after", "watchdog_reset", "write-flash",
       "0x0", os.path.join(build, "bootloader.bin"),
       "0x8000", os.path.join(build, "partitions.bin"),
       "0xe000", boot_app0,
       "0x10000", os.path.join(build, "firmware.bin")]
print(" ".join(cmd))
sys.exit(subprocess.call(cmd))
