#!/usr/bin/env python3
"""Chat on the mesh from the computer, through the T-Deck's USB dongle.

    python3 tools/meshterm.py [/dev/cu.usbmodemXXXX]

Needs pyserial (pip install pyserial). Messages the T-Deck receives or sends show up
here; lines you type go out on the channel. "@6ee1 hello" sends a DM to node 6ee1.
The T-Deck's Keyboard app must have "Mesh: To dongle" on.
"""
import glob
import json
import sys
import threading
import time

try:
    import serial
except ImportError:
    sys.exit("pip install pyserial")


def pick_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
    if len(sys.argv) > 1:
        return sys.argv[1]
    if not ports:
        sys.exit("no USB serial port found; plug the dongle in or pass its port")
    if len(ports) > 1:
        print("several ports; pass the dongle's:\n  " + "\n  ".join(ports))
        sys.exit(1)
    return ports[0]


def show(line):
    if not line.startswith("{"):
        if line.startswith("[dongle]"):
            print(f"\033[2m{line}\033[0m")
        return
    try:
        j = json.loads(line)
    except ValueError:
        return
    t = j.get("type")
    if t == "text":
        when = time.strftime("%H:%M", time.localtime(j["time"])) if j.get("time") else "--:--"
        tag = " (DM)" if j.get("dm") else ""
        print(f"[{when}] {j.get('name', j.get('from'))}{tag}: {j.get('text', '')}")
    elif t == "link":
        print(f"\033[2m-- dongle {j.get('state')}" + (f", T-Deck {j['tdeck']}" if j.get("tdeck") else "") + "\033[0m")
    elif t == "error":
        print(f"!! {j.get('text')}")
    elif t == "file":
        print(f"\033[2m-- receiving {j.get('name')} ({j.get('size')} bytes)\033[0m")
    elif t == "sync":
        print("\033[2m-- notes synced; the card is back on the desktop\033[0m")


def reader(s):
    buf = b""
    while True:
        buf += s.read(4096)
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            show(line.decode("utf-8", "replace").strip())


def main():
    port = pick_port()
    s = serial.Serial(port, 115200, timeout=0.1)
    print(f"-- {port}; type a line and press Enter to send on the channel, Ctrl-C to quit")
    threading.Thread(target=reader, args=(s,), daemon=True).start()
    try:
        for line in sys.stdin:
            line = line.rstrip("\n")
            if not line.strip():
                continue
            msg = {"send": line}
            if line.startswith("@"):
                to, _, rest = line[1:].partition(" ")
                msg = {"send": rest, "to": to}
            s.write((json.dumps(msg) + "\n").encode())
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
