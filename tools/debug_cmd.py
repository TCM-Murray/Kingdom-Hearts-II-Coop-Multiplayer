"""Test command for a copy started with KH2COOP_DEBUG=1 (accepted from this PC only).

Usage: py tools/debug_cmd.py <copy's listen port, e.g. 27701> damage <who: 0 Sora, 1 companion 1, 2 companion 2, 3 the other player's Sora copy> <amount>
       py tools/debug_cmd.py <port> motion <who> <motion id>
The damage goes through the mod's HP hook like an enemy hit, so KOs, deaths, forwarding and the downed rule happen as in play.
"""
import socket
import struct
import sys

port, cmd, who, amount = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
assert cmd in ("damage", "motion")
value = amount if cmd == "damage" else -(amount + 1)  # the DLL reads a negative amount as a motion id
socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(struct.pack("<Iii", 0x4432484B, who, value), ("127.0.0.1", port))
print(f"sent: {cmd} {amount} to {who}")
