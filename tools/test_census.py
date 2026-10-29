"""World mirror research (TODO 1.55): a bench pair in a boss room with the actor census and the script call log
on (KH2COOP_CENSUS=1, KH2COOP_TRAP_LOG=1, src/dll/world_census.cpp), so both games' logs show what each one has
and does.

Usage: py tools/test_census.py <scenario> [--seconds N] [--shot-every S] [--no-triangle] [--friend-attacks]
                                [--magic] [--cast] [--walk X,Z ...] [--env KEY=VALUE ...]

Scenarios (save from the co-op save copy, then the host warps; programs map/battle/event from the session 29
two-PC log):
  bc       Beast's Castle ballroom 0x05/0x05 program 78 (Shadow Stalker; then 79, Dark Thorn), save 13
  bc2      the same room, program 79 (Dark Thorn) directly
  twins    Agrabah 0x07/0x03 program 59 (Volcanic Lord and Blizzard Lord), save 20
  shanyu   Land of Dragons 0x08/0x09 program 75 (Shan-Yu), save 10
  gusts    Land of Dragons 0x08/0x08 programs 0/1/0 (the room before the summit), save 10
  borough  Hollow Bastion Borough 0x04/0x09 programs 1/1/0 (first visit: Shadows, Soldiers), save 97; both Soras
           walk onto the street, into the 4 Shadows' trigger (group 245)
  neo      the Borough with battle program 4 (second visit: Neoshadows M_EX420), save 97; both are teleported
           into the 2 Neoshadows' trigger (group 550, z -2367; test command, walking there gets stuck)
  data_*, ex400_*, ex370_*, ex420_*  the Data fights and the bosses B_EX400 / B_EX370 / B_EX420 (effect pool,
           TODO 1.59; rooms below SCENARIOS), save 45
  room     no warp: stay in the save's room (--slot N)
--walk X,Z (repeatable) replaces the scenario's walk: both Soras walk these points in turn after the warp.
--teleport X,Y,Z replaces the scenario's teleport (before the walk; y grows downward).

Parity (TODO 1.55, one brain): with --env KH2COOP_PARITY=1 the run ends with tools/parity_compare.py on the two
logs.

--only-friend: only the friend's Sora attacks; the host's stands still.
--friend-menu valor|limit|summon: the friend uses it --menu-at seconds into the fight (TODO 4.4: do its kills count on
the host); --env KH2COOP_TRACE_STAT2=1 logs the stat change twin (exe+0x3D2C50) calls.
--lock-on: the attackers keep a lock-on: R1 whenever their Sora isn't locked (a normal enemy's HP bar shows while
locked on). --chase: an attacker farther than 500 from the nearest enemy is put 150 from it (test command 15),
farther than 250 walks to it (bosses: no hitting walls; you 2026-10-13: "force lock-on so Soras can hit them").
--bar-watch: both games' enemy HP bar is sampled during the fight (does it show the enemy's HP; TODO 1.21).
--pool: both games' effect pool is sampled during the fight (peak bytes in use; TODO 1.30).

The host's Sora attacks (cross) and presses triangle now and then (reaction commands, Limits; not with
--no-triangle); with --friend-attacks the friend's does too. With --magic the host's Sora uses its four shortcuts
(L1 + square / triangle / circle / cross) in turn instead of attacking; with --cast it casts the spells of its
six spell slots in turn through the test command (no menu, no MP; --cast-by friend: the friend's Sora does). God
mode on. Screenshots (every 20 s, or --shot-every)
and a summary go to build/bench/census/.
The real Steam save is never written (SAVE_GUARD=1); the co-op save copy is only read.
"""
import argparse
import math
import os
import re
import struct
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench  # noqa: E402
import peek  # noqa: E402
import scene  # noqa: E402
import vpad  # noqa: E402

OUT = os.path.join(bench.ROOT, "build", "bench", "census")
# The field command menu (VERIFIED_OFFSETS 'Reaction commands: where the offer goes'): *(*(exe+0x2A105D0) + 0xDD0).
# Its four top slots are 0x24 bytes each from +0xA1C (+2 = the command id).
PLAYER_PTR, MENU_OF_PLAYER, MENU_SLOTS = 0x2A105D0, 0xDD0, 0xA1C


class Picker:
    """--pick N: when command N shows in one of a copy's four top menu slots (a boss's trap_command_override_top,
    e.g. Data Roxas's 672 'The End' among three '?'), move the cursor to it and press cross at once: the slot is
    open ~0.5 s and the main loop's taps take 250 ms each. The main loop's pad steps send this one's buttons meanwhile
    (vpad.Pad.override). The menu wraps, and its cursor goes back to the top slot after a command is used (stays
    put otherwise; bench 2026-10-14, screenshot data_roxas_B_09), so the picker keeps track of where it is."""

    def __init__(self, c, cmd):
        self.c, self.cmd = c, cmd
        self.events = []  # one line per pick
        self.pos = 0      # the cursor's slot (the top one when the fight starts)
        self.polls = self.seen = self.errors = self.used = 0
        self.done = False
        self.th = threading.Thread(target=self.run, daemon=True)
        self.th.start()

    def menu(self):
        h, base = self.c.game.h, self.c.game.base
        player = struct.unpack("<Q", peek.read(h, base + PLAYER_PTR, 8))[0]
        return struct.unpack("<Q", peek.read(h, player + MENU_OF_PLAYER, 8))[0] if player else 0

    def slots(self, menu):
        raw = peek.read(self.c.game.h, menu + MENU_SLOTS, 4 * 0x24)
        return [struct.unpack_from("<H", raw, n * 0x24 + 2)[0] for n in range(4)]

    def tap(self, button, ms=50):
        pad = self.c.game.pad
        pad.override = vpad.BITS[button]
        pad.send(vpad.BITS[button], ms=ms, own=True)
        pad.override = 0
        pad.send(0, ms=ms, own=True)

    def log_size(self):
        try:
            return os.path.getsize(self.c.log)
        except OSError:
            return 0

    def used_since(self, offset):
        """The copy's log says 'we used command <cmd>' after offset (reaction_sync.cpp)."""
        try:
            with open(self.c.log, encoding="utf-8", errors="replace") as fh:
                fh.seek(offset)
                return f"we used command {self.cmd} " in fh.read()
        except OSError:
            return False

    def run(self):
        last = 0.0
        while not self.done:
            self.polls += 1
            try:
                menu = self.menu()
                ids = self.slots(menu) if menu else []
            except OSError:
                self.errors += 1
                time.sleep(0.05)
                continue
            if self.cmd not in ids:
                time.sleep(0.01)
                continue
            self.seen += 1
            if time.time() - last < 2:  # one pick per offer
                time.sleep(0.01)
                continue
            last = time.time()
            k = ids.index(self.cmd)
            down, up = (k - self.pos) % 4, (self.pos - k) % 4
            moves = ["down"] * down if down <= up else ["up"] * up
            try:
                t0, mark = time.time(), self.log_size()
                for b in moves + ["cross"]:
                    self.tap(b)
                self.c.game.pad.override = None
                took = time.time() - t0
                bench.screenshot(self.c.hwnd, os.path.join(OUT, f"pick{len(self.events):02d}_{self.c.name}.png"))
                time.sleep(0.5)
                used = self.used_since(mark)
                self.used += used
                start, self.pos = self.pos, 0 if used else k
                self.events.append(f"{time.strftime('%H:%M:%S')} slots {ids}: cursor {start} -> slot {k} "
                                   f"({' '.join(moves) or 'no move'}, cross) in {took:.2f} s: "
                                   f"{'used' if used else 'not used'}")
            except OSError as e:
                self.events.append(f"read error during a pick: {e!r}")
            finally:
                self.c.game.pad.override = None

    def summary(self):
        self.done = True
        self.th.join(2)
        return (f"{self.c.name} picker for command {self.cmd}: {self.polls} polls, {self.seen} with it in a slot, "
                f"{len(self.events)} picks, {self.used} used, read errors {self.errors}" + "".join("\n  " + e for e in self.events))


# scenario: (save, the room that save loads, warps)
SCENARIOS = {
    "bc": (13, (0x05, 0x01), [(0x05, 0x05, 0, 78, 78, 78)]),
    "bc2": (13, (0x05, 0x01), [(0x05, 0x05, 0, 79, 79, 79)]),
    "twins": (20, (0x07, 0x02), [(0x07, 0x03, 0, 59, 59, 59)]),
    "shanyu": (10, (0x08, 0x00), [(0x08, 0x09, 0, 75, 75, 75)]),
    "gusts": (10, (0x08, 0x00), [(0x08, 0x08, 0, 0, 1, 0)]),
    "borough": (97, (0x04, 0x0D), [(0x04, 0x09, 2, 1, 1, 0)]),
    "neo": (97, (0x04, 0x0D), [(0x04, 0x09, 2, 1, 4, 0)]),
    # Betwixt and Between (the gym room, debug_cmd.py gym): battle program 160 = group b_40 (mission TT40_MS002),
    # 161 = b_41 (TT40_MS003), from tt40.ard's btl script. b_40 and b_41 place the party themselves, so the map
    # program is the same one (160 / 161 place no one; map program 0 made a second Sora). Save 59 = Hollow Bastion
    # 0x04/0x06, a high level
    "betwixt": (59, (0x04, 0x06), [(0x02, 0x28, 0, 160, 160, 0)]),
    "betwixt2": (59, (0x04, 0x06), [(0x02, 0x28, 0, 161, 161, 0)]),
    # with event program 160 too: the story's own chain (mission end -> cutscene 011 -> program 161, b_41); with
    # event program 0 nothing follows the mission's end and the camera stays held
    "betwixt_story": (59, (0x04, 0x06), [(0x02, 0x28, 0, 160, 160, 160)]),
    # the gym (debug_cmd.py gym): Betwixt and Between with no programs, no enemies; add them with --spawn
    "gym": (59, (0x04, 0x06), [(0x02, 0x28, 0, 0, 0, 0)]),
    # the second gym (debug_cmd.py gym arena): the small Underdrome 06/0D, no programs (no edge); Goofy and Auron
    "arena": (59, (0x04, 0x06), [(0x06, 0x0D, 0, 0, 0, 0)]),
    "room": (None, None, []),
}
# Boss fights for the effect pool (TODO 1.59; rooms and programs from the ard/us spawn points, session 33; the
# names in tools/pool_peaks.py FIGHTS), each from save 45 (The World That Never Was, 0x12/0x12).
for _name, (_w, _r, _p) in {
    "data_axel": (0x02, 0x14, 213), "data_demyx": (0x04, 0x04, 114), "data_xaldin": (0x05, 0x0F, 97),
    "data_xigbar": (0x12, 0x0A, 100), "data_luxord": (0x12, 0x0E, 101), "data_saix": (0x12, 0x0F, 102),  # names fixed 2026-10-17 (were swapped)
    "data_xemnas": (0x12, 0x13, 97), "data_xemnas2": (0x12, 0x14, 98), "data_roxas": (0x12, 0x15, 99),
    "ex400_143": (0x04, 0x21, 143), "ex400_148": (0x04, 0x21, 148), "ex400_155": (0x04, 0x21, 155),
    "ex370_151": (0x04, 0x22, 151), "ex370_152": (0x04, 0x22, 152),
    "ex420_67": (0x0C, 0x07, 67), "ex420_73": (0x0C, 0x07, 73), "armored2": (0x12, 0x17, 73),
}.items():
    SCENARIOS[_name] = (45, (0x12, 0x12), [(_w, _r, 0, _p, _p, _p)])
# Where both Soras walk after the warp (x, z; spawn groups from ard/us/hb09.ard, VERIFIED_OFFSETS 'Borough
# battle programs'). Door 2 puts them at (-605, -450); straight off the door spot first (scene.py street).
WALKS = {
    "borough": [(1000, -500), (400, -500)],
}
# --friend-menu: the friend's command menu keys (from tools/test_extra_copy.py forms / limit / summon); each step
# is (keys, then seconds to wait).
DRIVE = ["tap", "down", "wait", "300"] * 3  # Attack -> Magic -> Items -> Drive
PAGE2 = DRIVE + ["tap", "left", "wait", "1000"]  # Attack / Summon / Party / Limit, cursor on Limit
MENUS = {
    "valor": [(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"], 2)],
    "limit": [(PAGE2, 1)] + [(["tap", "cross", "wait", "300"], 2.5)] * 4,
    "summon": [(PAGE2 + ["tap", "up", "wait", "500", "tap", "up", "wait", "500"], 1)] + [(["tap", "cross", "wait", "300"], 2.5)] * 4,
}
# Teleports (test command 15) before the walk: x, y (a little above the ground), z.
TELEPORTS = {
    "neo": (466, -30, -2300),
}


def warp(world, room, door, map_, btl, evt):
    bench.debug("A", 12, world, room, door, 0, map_, btl, evt, 0, fmt="<IiBBBBHHHH")


SAVE = 0x9A98B0  # the live save (VERIFIED_OFFSETS: EXP save+0x36E0; Drive Form records save+0x32F4, 0x38 each)
FORMS = ("Valor", "Wisdom", "Limit", "Master", "Final")


def save_exp(c):
    """Sora's EXP and each Drive Form's (level, EXP): record +2 level (u8), +4 EXP (u32), OpenKH SaveDataFinalMix."""
    h, base = c.game.h, c.game.base
    out = {"EXP": struct.unpack("<I", peek.read(h, base + SAVE + 0x36E0, 4))[0]}
    for i, name in enumerate(FORMS):
        rec = peek.read(h, base + SAVE + 0x32F4 + i * 0x38, 8)
        out[name] = (rec[2], struct.unpack("<I", rec[4:8])[0])
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scenario", choices=sorted(SCENARIOS))
    ap.add_argument("--seconds", type=int, default=120)
    ap.add_argument("--slot", type=int)
    ap.add_argument("--friend-attacks", action="store_true")
    ap.add_argument("--only-friend", action="store_true", help="only the friend's Sora attacks (the host's stands)")
    ap.add_argument("--shot-every", type=float, default=20)
    ap.add_argument("--no-triangle", action="store_true")
    ap.add_argument("--triangle-every", type=float, default=6,
                    help="seconds between triangle presses (reaction commands; 0 = every step, ~0.5 s)")
    ap.add_argument("--triangle-by", choices=["all", "host", "friend"], default="all",
                    help="who presses triangle (Limits, reaction commands); with --friend-attacks both attack")
    ap.add_argument("--magic", action="store_true", help="the host uses its shortcuts (spells) instead of attacking")
    ap.add_argument("--cast", action="store_true", help="the host casts its six spell slots in turn (test command)")
    ap.add_argument("--cast-by", choices=["host", "friend"], default="host", help="who casts with --cast")
    ap.add_argument("--walk", action="append", default=[], help="X,Z: a point both Soras walk to after the warp")
    ap.add_argument("--teleport", help="X,Y,Z: both Soras are put there after the warp (before the walk)")
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--friend-menu", choices=sorted(MENUS),
                    help="the friend goes into Valor Form / uses its first Limit / its first summon --menu-at s into the fight")
    ap.add_argument("--menu-at", type=float, default=5)
    ap.add_argument("--menu-by", choices=("friend", "host", "both"), default="friend")  # who uses --friend-menu
    ap.add_argument("--both-offset", type=float, default=0)  # --menu-by both: the host starts this many s later
    ap.add_argument("--both-after", choices=("friend", "host"))  # --menu-by both: the other uses its menu during this one's
    ap.add_argument("--lock-on", action="store_true", help="the attackers press R1 whenever they aren't locked on")
    ap.add_argument("--chase", action="store_true", help="the attackers walk to the nearest enemy when it's out of reach")
    ap.add_argument("--bar-watch", action="store_true",
                    help="sample both games' enemy HP bar during the fight: does it show the enemy's HP (TODO 1.21)")
    ap.add_argument("--pool", action="store_true", help="sample both games' effect pool during the fight (TODO 1.30)")
    ap.add_argument("--pick", type=int, help="the friend picks this command as soon as a boss puts it in its top "
                    "menu (Data Roxas's 672 among four '?', TODO 1.62)")
    ap.add_argument("--host-spin", type=float, help="the host's camera turns all the time at this right-stick speed "
                    "(0-1), so the fight goes in and out of its view (grab trace, TODO 1.62)")
    ap.add_argument("--exp", action="store_true",
                    help="read both games' EXP and the Drive Forms' level and EXP before and after the fight (TODO 4.5)")
    ap.add_argument("--boss-hp", type=int,
                    help="the host's bosses get this HP just before --friend-menu (debug command), so a Limit, "
                         "summon or form can finish them (TODO 4.4)")
    ap.add_argument("--hp-at", action="append", default=[],
                    help="T:HP: the host's bosses get HP at T s into the fight (debug command), e.g. to reach a "
                         "boss's later stages; repeatable")
    ap.add_argument("--spawn", action="append", default=[],
                    help="OBJ:COUNT@T: the host makes COUNT of object OBJ (hex) in front of its Sora T s into the fight "
                         "(debug command 'spawn', TODO 2.5), e.g. 13E:1@2 (a Dusk); repeatable")
    a = ap.parse_args()
    walk = [tuple(float(v) for v in w.split(",")) for w in a.walk] or WALKS.get(a.scenario, [])
    teleport = tuple(float(v) for v in a.teleport.split(",")) if a.teleport else TELEPORTS.get(a.scenario)
    os.makedirs(OUT, exist_ok=True)
    slot, room, warps = SCENARIOS[a.scenario]
    if a.slot:
        slot, room = a.slot, None
    if not slot:
        raise SystemExit("--slot needed")
    out = [f"scenario {a.scenario}, save {slot}, warps {warps}, {a.seconds} s"]
    copies = bench.launch(extra=["KH2COOP_CENSUS=1", "KH2COOP_TRAP_LOG=1"] + a.env)
    host, friend = copies
    ok = False
    try:
        marks = {c.name: c.mark() for c in copies}
        bench.load_slot_both(copies, slot)  # both games at once
        for c in copies:
            # A press lost in the save list loads the save next to it (bench 2026-10-12 18:25: the friend got save 12,
            # Donald instead of Beast). The friend follows the host's room within ~2 s, so take the first room its
            # log shows after the pick.
            first = next((re.search(r"world 0x(..) room 0x(..)", l) for l in c.since(marks[c.name])
                          if re.search(r" frame \d+ world 0x.. room 0x..", l) and "world 0xFF" not in l), None)
            loaded = (int(first.group(1), 16), int(first.group(2), 16)) if first else None
            if room and loaded != room:
                raise bench.Failed(f"copy {c.name} loaded the wrong save: first room {loaded}, save {slot} is {room}")
        bench.wait("both in the save's room", lambda: host.room() == friend.room(), 60, copies)
        time.sleep(6)
        for w in warps:
            warp(*w)
            bench.wait(f"both in 0x{w[0]:02X}/0x{w[1]:02X}",
                       lambda: all(c.room() == (w[0], w[1]) and c.game.sora() for c in copies), 90, copies)
        time.sleep(8)
        for c in copies:  # the host first: its game spawns the groups
            if teleport:
                bench.debug(c.name, 15, *teleport, fmt="<Iifff")
                time.sleep(1.5)
                out.append(f"{c.name} teleport to {teleport}: {c.game.where()}")
            for x, z in walk:
                result = c.game.goto(x, z, 60, 12)
                out.append(f"{c.name} walk to ({x:.0f}, {z:.0f}): {result} | {c.game.where()}")
        marks = {c.name: c.mark() for c in copies}
        exp_before = {c.name: save_exp(c) for c in copies} if a.exp else None
        bars = bench.BarWatch(copies) if a.bar_watch else None
        pools = bench.PoolWatch(copies) if a.pool else None
        picker = Picker(friend, a.pick) if a.pick else None
        spinning = [bool(a.host_spin)]
        if a.host_spin:
            def spin():
                while spinning[0]:
                    host.game.pad.send(rx=a.host_spin, ms=500, own=True)
                host.game.pad.send(own=True)
            threading.Thread(target=spin, daemon=True).start()
        start = time.time()
        shot = 0
        casts = 0
        chased, locks, read_errors = {"A": 0, "B": 0}, {"A": 0, "B": 0}, {}
        lockstats = {"A": [0, 0, 0], "B": [0, 0, 0]}
        last_triangle = {"A": -99.0, "B": -99.0}  # lock-on state seen per step: none, auto target, locked
        menu_done = not a.friend_menu
        spawn_at = sorted((float(v.split("@")[1]), int(v.split(":")[0], 16), int(v.split(":")[1].split("@")[0]))
                          for v in a.spawn)
        hp_at = sorted((float(v.split(":")[0]), int(v.split(":")[1])) for v in a.hp_at)
        while time.time() - start < a.seconds:
            t = time.time() - start
            if not menu_done and t >= a.menu_at:
                menu_done = True
                if a.boss_hp is not None:
                    bench.debug("A", 13, a.boss_hp)
                    out.append(f"host's bosses set to {a.boss_hp} HP at {t:.0f} s")
                users = [friend, host] if a.menu_by == "both" else [host if a.menu_by == "host" else friend]
                fms = [u.mark() for u in users]
                errors = []

                def use_menu(u, delay):
                    try:
                        time.sleep(delay)
                        for keys, wait in MENUS[a.friend_menu]:
                            u.game.pad.run(keys)
                            time.sleep(wait)
                    except Exception as e:  # noqa: BLE001 (a thread: report it in the summary)
                        errors.append(f"{u.name}: {e!r}")

                if a.menu_by == "both" and a.both_after:
                    # no threads: the one named by --both-after uses its whole menu (its Limit starts and runs on),
                    # then the other uses its menu while that Limit still runs
                    first, second = (host, friend) if a.both_after == "host" else (friend, host)
                    use_menu(first, 0)
                    use_menu(second, 0)
                else:
                    # both: each pad in its own thread; the host starts --both-offset s after the friend
                    threads = [threading.Thread(target=use_menu, args=(u, a.both_offset if u is host and len(users) > 1 else 0))
                               for u in users]
                    for th in threads:
                        th.start()
                    for th in threads:
                        th.join()
                if errors:
                    out.append(f"menu errors: {errors}")
                for c in copies:
                    bench.screenshot(c.hwnd, os.path.join(OUT, f"{a.scenario}_{a.friend_menu}_{c.name}.png"))
                for u, fm in zip(users, fms):
                    cmds = [l[13:110] for l in u.since(fm) if "carries out command" in l or "menu: command" in l]
                    out.append(f"{u.name}'s {a.friend_menu} at {t:.0f} s: commands {cmds[:6]}")
                continue
            if spawn_at and t >= spawn_at[0][0]:
                _, obj, count = spawn_at.pop(0)
                bench.debug("A", 19, obj | count << 16)
                out.append(f"host: spawn {obj:#x} x{count} at {t:.0f} s")
            if hp_at and t >= hp_at[0][0]:
                bench.debug("A", 13, hp_at[0][1])
                out.append(f"host's bosses set to {hp_at[0][1]} HP at {t:.0f} s")
                hp_at.pop(0)
            if t >= shot * a.shot_every:
                for c in copies:
                    bench.screenshot(c.hwnd, os.path.join(OUT, f"{a.scenario}_{c.name}_{shot:02d}.png"))
                shot += 1
            fighters = [friend] if a.only_friend else copies if a.friend_attacks else [host]
            for c in fighters:
                if a.cast and c is host:
                    bench.debug("A" if a.cast_by == "host" else "B", 14, casts % 6)
                    c.game.pad.run(["wait", "900"])
                    for d in copies:  # the spell on both screens
                        bench.screenshot(d.hwnd, os.path.join(OUT, f"cast{casts:02d}_{d.name}.png"))
                    casts += 1
                    c.game.pad.run(["wait", "1600"])
                    continue
                if a.magic and c is host:
                    face = ("square", "triangle", "circle", "cross")[casts % 4]
                    casts += 1
                    c.game.pad.run(["hold", "l1", "400", "tap", "l1+" + face, "150", "wait", "1200"])
                    continue
                try:
                    if a.chase:
                        s, foes = c.game.sora(), c.game.enemies()
                        if s and foes:
                            x, y, z = min(foes, key=lambda e: math.hypot(e[0] - s[0], e[2] - s[2]))
                            d = math.hypot(x - s[0], z - s[2])
                            if d > 500:  # bosses that warp away (Xigbar): put our Sora 150 from it, on our side
                                k = 150 / d
                                bench.debug(c.name, 15, x + (s[0] - x) * k, y - 30, z + (s[2] - z) * k, fmt="<Iifff")
                                time.sleep(0.3)
                                chased[c.name] += 1
                            elif d > 250:
                                c.game.goto(x, z, 180, 1.0)
                                chased[c.name] += 1
                    if a.lock_on:
                        state = c.game.lock()
                        if state in (0, 1, 2):
                            lockstats[c.name][state] += 1
                        if state != 2:  # R1 toggles: only when not locked
                            c.game.pad.run(["tap", "r1", "wait", "150"])
                            locks[c.name] += 1
                except OSError as e:  # an actor freed while read
                    read_errors[c.name] = read_errors.get(c.name, 0) + 1
                    read_errors.setdefault("first", repr(e))
                triangle = (t - last_triangle[c.name] >= a.triangle_every and not a.no_triangle
                            and a.triangle_by in ("all", "host" if c is host else "friend"))
                if triangle:
                    last_triangle[c.name] = t
                c.game.pad.run(["tap", "triangle" if triangle else "cross", "wait", "250"])
            for c in copies:
                c.lines()
                if c.crash or not c.alive():
                    raise bench.Failed(f"copy {c.name}: {c.crash or 'closed'}")
            time.sleep(0.3)
        spinning[0] = False
        if a.chase or a.lock_on:
            out.append(f"walks to the nearest enemy {chased}, R1 presses to lock on {locks}, read errors {read_errors}")
            out.append(f"lock-on state per step (none / auto target / locked): {lockstats}")
        if exp_before:
            for c in copies:
                after = save_exp(c)
                out.append(f"{c.name} EXP and forms (before -> after): " + ", ".join(
                    f"{k} {exp_before[c.name][k]} -> {after[k]}" for k in after))
        if bars:
            bars.stop()
            for c in copies:
                out.append(f"{c.name} enemy HP bar: {bars.summary(c.name)}")
        if picker:
            out.append(picker.summary())
        if pools:
            pools.stop()
            for c in copies:
                out.append(f"{c.name} effect pool: {pools.summary(c.name)}")
        hits = [l for l in host.since(marks["A"]) if "friend hit our#" in l]
        out.append(f"host: friend's hits applied {len(hits)}, last: {[l[:120] for l in hits[-3:]]}")
        for c in copies:
            twin = [l for l in c.since(marks[c.name]) if "stat change twin on" in l]
            if twin:
                out.append(f"{c.name}: stat change twin calls {len(twin)}: {[l[13:160] for l in twin[:6]]}")
        for c in copies:
            new = c.since(marks[c.name])
            traps = [l for l in new if " trap " in l[:20]]
            census = [l for l in new if "census:" in l]
            out.append(f"{c.name}: log {c.log}, {len(new)} lines in the fight ({len(traps)} trap calls, "
                       f"{len(census)} census), room {c.room()}, alive {c.alive()}, crash {c.crash or '-'}")
        ok = all(c.alive() and not c.crash for c in copies)
    except bench.Failed as e:
        out.append(f"FAILED: {e}")
    finally:
        bench.close(copies)
    if "KH2COOP_PARITY=1" in a.env:
        r = subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "parity_compare.py"),
                            host.log, friend.log], capture_output=True, text=True)
        out.append("parity_compare.py:\n" + (r.stdout + r.stderr).rstrip())
    out.append("PASS" if ok else "FAIL")
    text = "\n".join(out)
    print(text)
    with open(os.path.join(OUT, f"{a.scenario}.txt"), "w", encoding="utf-8") as fh:
        fh.write(text + "\n")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
