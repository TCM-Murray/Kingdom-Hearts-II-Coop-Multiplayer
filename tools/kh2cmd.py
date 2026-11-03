"""Test command v2 (TODO 2.1): one text command to a copy started with KH2COOP_DEBUG=1, and its answer.

Usage: py tools/kh2cmd.py <copy's listen port, e.g. 27701> <command ...>
Commands: ping | actors | actor <address hex>   (avatar_link.cpp DevCommand; this PC only)
The answer is one packet per line, then "end <lines>". No answer = the copy isn't running, wasn't started with
KH2COOP_DEBUG=1, or co-op is off. The older one-way commands stay in tools/debug_cmd.py.
"""
import random
import socket
import struct
import sys

MAGIC = 0x7632484B  # "KH2v"


def query(port, text, timeout=1.0):
    """The answer's lines, or None when no answer came."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)  # a full actor list is a few hundred packets
    s.bind(("127.0.0.1", 0))
    s.settimeout(timeout)
    seq = random.getrandbits(32)
    lines = []
    try:
        s.sendto(struct.pack("<II", MAGIC, seq) + text.encode(), ("127.0.0.1", port))
        while True:
            data = s.recv(2048)
            if len(data) < 8 or struct.unpack_from("<II", data) != (MAGIC, seq):
                continue
            line = data[8:].decode("utf-8", "replace")
            if line.startswith("end "):
                if int(line[4:]) != len(lines):
                    raise OSError(f"{len(lines)} of {line[4:]} lines arrived")
                return lines
            lines.append(line)
    except (socket.timeout, ConnectionResetError):  # reset: nothing listens on that port
        return None
    finally:
        s.close()


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    answer = query(int(sys.argv[1]), " ".join(sys.argv[2:]))
    if answer is None:
        sys.exit(f"no answer from port {sys.argv[1]}")
    print("\n".join(answer))
