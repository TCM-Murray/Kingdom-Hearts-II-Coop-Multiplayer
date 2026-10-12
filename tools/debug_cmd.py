"""Test command for a copy started with KH2COOP_DEBUG=1 (accepted from this PC only).

Usage: py tools/debug_cmd.py <copy's listen port, e.g. 27701> damage <who: 0 Sora, 1 companion 1, 2 companion 2, 3 the other player's Sora copy> <amount>
       py tools/debug_cmd.py <port> motion <who> <motion id>
       py tools/debug_cmd.py <port> crown <who> <value hex, e.g. 9C; FFFF = the game's own pick from the Proofs>
         (crown also takes who 4 = every Sora actor of the running cutscene)
       py tools/debug_cmd.py <port> link <choice: 1 wait, 2 leave to the title screen, 3 play on with Donald and Goofy>
         (the choice while the other game is gone, TODO 6.1; the on-screen menu comes later)
         (4 = test only: back to the title screen now, also while connected; the same safe path as 2)
       py tools/debug_cmd.py <port> double
         (asks the game for a reload of our room twice in one frame: the second one must be dropped, TODO 6.10)
       py tools/debug_cmd.py <port> warp <world hex> <room hex> [door hex] [map btl evt]
         (load that room once the game is safe; programs default to the save's (65535); use it on the host,
         the friend follows; TODO 2.2)
       py tools/debug_cmd.py <port> bosshp <hp>
         (every boss in the room (model B_...) gets that HP; host side)
       py tools/debug_cmd.py <port> cast <slot 0-5>
         (our Sora casts the spell in that spell slot, without the menu or MP; the log lists the slots)
God mode is a launch setting, not a command: KH2COOP_GOD=1 (hits on the party cost no HP).
The damage goes through the mod's HP hook like an enemy hit, so KOs, deaths, forwarding and the downed rule happen as in play.
"""
import socket
import struct
import sys

port, cmd = int(sys.argv[1]), sys.argv[2]
if cmd == "link":
    choice = int(sys.argv[3])
    socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(struct.pack("<Iii", 0x4432484B, 10, choice), ("127.0.0.1", port))
    print(f"sent: link choice {choice}")
    sys.exit(0)
if cmd == "warp":
    world, room = int(sys.argv[3], 16), int(sys.argv[4], 16)
    door = int(sys.argv[5], 16) if len(sys.argv) > 5 else 0
    progs = [int(v) for v in sys.argv[6:9]] if len(sys.argv) > 8 else [0xFFFF] * 3
    pkt = struct.pack("<IiBBBBHHHH", 0x4432484B, 12, world, room, door, 0, *progs, 0)
    socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(pkt, ("127.0.0.1", port))
    print(f"sent: warp to world 0x{world:02X} room 0x{room:02X} door 0x{door:02X} programs {progs}")
    sys.exit(0)
if cmd == "bosshp":
    hp = int(sys.argv[3])
    socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(struct.pack("<Iii", 0x4432484B, 13, hp), ("127.0.0.1", port))
    print(f"sent: boss HP {hp}")
    sys.exit(0)
if cmd == "cast":
    slot = int(sys.argv[3])
    socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(struct.pack("<Iii", 0x4432484B, 14, slot), ("127.0.0.1", port))
    print(f"sent: cast slot {slot}")
    sys.exit(0)
if cmd == "double":
    socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(struct.pack("<Iii", 0x4432484B, 11, 0), ("127.0.0.1", port))
    print("sent: two room requests in one frame")
    sys.exit(0)
who = int(sys.argv[3])
assert cmd in ("damage", "motion", "crown")
amount = int(sys.argv[4], 16) if cmd == "crown" else int(sys.argv[4])
# the DLL reads a negative amount as a motion id, bit 30 as a crown value
value = amount if cmd == "damage" else -(amount + 1) if cmd == "motion" else 0x40000000 | amount
socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(struct.pack("<Iii", 0x4432484B, who, value), ("127.0.0.1", port))
print(f"sent: {cmd} {amount} to {who}")
