#!/usr/bin/env python3
"""Save the GEEK dongle's screen as a PNG.

    python3 common/tools/geekshot.py /dev/cu.usbmodemXXXX out.png [scale]

Asks the dongle for {"get":"screen"} and writes the 240x135 RGB565 framebuffer as a PNG,
scaled up by an integer factor (default 1). Needs pyserial only.
"""
import base64
import json
import struct
import sys
import time
import zlib

import serial


def png(path, w, h, rgb, scale):
    raw = bytearray()
    for y in range(h):
        row = rgb[y * w * 3:(y + 1) * w * 3]
        if scale > 1:
            row = b"".join(row[x * 3:x * 3 + 3] * scale for x in range(w))
        line = b"\x00" + bytes(row)
        raw += line * scale
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale, h * scale, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


def main():
    port, out = sys.argv[1], sys.argv[2]
    scale = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    s = serial.Serial(port, 115200, timeout=0.5)
    s.reset_input_buffer()
    s.write(b'{"get":"screen"}\n')
    buf, t = b"", time.time()
    while time.time() - t < 5:
        buf += s.read(65536)
        for line in buf.split(b"\n")[:-1]:  # complete lines only
            if b'"type":"screen"' in line:
                j = json.loads(line)
                px = base64.b64decode(j["b64"])
                w, h = j["w"], j["h"]
                rgb = bytearray()
                for i in range(0, len(px), 2):
                    v = (px[i] << 8) | px[i + 1]
                    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
                    rgb += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
                png(out, w, h, rgb, scale)
                print(f"saved {out} ({w * scale}x{h * scale})")
                return
    sys.exit("no screen answer; is this the dongle's port and is it a GEEK build?")


if __name__ == "__main__":
    main()
