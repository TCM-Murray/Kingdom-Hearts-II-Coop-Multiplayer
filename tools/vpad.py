"""Drive a KH2 copy launched with KH2COOP_VPAD=<port> from a script (no window focus needed).

Usage: py tools/vpad.py <port> <step> [<step> ...]
A step is one of:
  wait <ms>                       neutral pad for ms
  tap <buttons> [ms=100]          press buttons, then release (and wait 150 ms)
  hold <buttons> <ms>             press buttons for ms
  stick <lx> <ly> <ms> [buttons]  left stick (-1..1, +y = up) for ms, optionally with buttons held
  cam <rx> <ry> <ms>              right stick for ms
Buttons: names joined by '+': cross circle square triangle start select l1 r1 l2 r2 l3 r3 up down left right
(KH2 on PC: cross = A (confirm/attack), circle = B (jump), triangle = Y, square = X.)

The DLL returns to neutral 30 frames after the last packet, so this script
resends the state every frame while a step lasts.
"""
import socket
import sys
import time

BITS = {"select": 0x0001, "l3": 0x0002, "r3": 0x0004, "start": 0x0008, "up": 0x0010, "right": 0x0020,
        "down": 0x0040, "left": 0x0080, "l2": 0x0100, "r2": 0x0200, "l1": 0x0400, "r1": 0x0800,
        "triangle": 0x1000, "circle": 0x2000, "cross": 0x4000, "square": 0x8000}


def buttons(spec):
    if not spec or spec == "none":
        return 0
    return sum(BITS[b] for b in spec.lower().split("+"))


def axis(v, invert):
    v = max(-1.0, min(1.0, float(v)))
    if invert:
        v = -v
    return max(0, min(255, round(128 + v * 127.5)))


class Pad:
    def __init__(self, port):
        self.addr = ("127.0.0.1", port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def send(self, btn=0, rx=0.0, ry=0.0, lx=0.0, ly=0.0, ms=0):
        pkt = b"KH2V" + btn.to_bytes(2, "little") + bytes([axis(rx, False), axis(ry, True),
                                                            axis(lx, False), axis(ly, True)])
        end = time.perf_counter() + ms / 1000
        while True:
            self.sock.sendto(pkt, self.addr)
            if time.perf_counter() >= end:
                break
            time.sleep(1 / 120)

    def run(self, steps):
        i = 0
        while i < len(steps):
            cmd = steps[i]
            if cmd == "wait":
                self.send(ms=int(steps[i + 1])); i += 2
            elif cmd == "tap":
                ms = int(steps[i + 2]) if i + 2 < len(steps) and steps[i + 2].isdigit() else 100
                self.send(buttons(steps[i + 1]), ms=ms)
                self.send(ms=150)
                i += 3 if ms != 100 or (i + 2 < len(steps) and steps[i + 2].isdigit()) else 2
            elif cmd == "hold":
                self.send(buttons(steps[i + 1]), ms=int(steps[i + 2])); i += 3
            elif cmd == "stick":
                btn = 0
                n = 4
                if i + 4 < len(steps) and steps[i + 4][0].isalpha() and steps[i + 4] not in (
                        "wait", "tap", "hold", "stick", "cam"):
                    btn = buttons(steps[i + 4]); n = 5
                self.send(btn, lx=steps[i + 1], ly=steps[i + 2], ms=int(steps[i + 3])); i += n
            elif cmd == "cam":
                self.send(rx=steps[i + 1], ry=steps[i + 2], ms=int(steps[i + 3])); i += 4
            else:
                raise SystemExit(f"unknown step {cmd!r}")
        self.send()  # leave neutral


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    Pad(int(sys.argv[1])).run(sys.argv[2:])
