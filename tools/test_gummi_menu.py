"""Bench for the Gummi menus in co-op (TODO 5.2, stage 1 items 2-4 of docs/GUMMI_COOP.md): save 96, by the open key hole.

Usage: py tools/test_gummi_menu.py [main] [nolist] [refused] [ex]     (default: all four; each starts its own pair)

  main     the Client's Start waits and Circle cancels; the Host's pick waits and Circle cancels; the pick reaches both
           games; Circle in ship selection sends both to the top menu; at the launch one Start waits and an Abort wins, from
           either player; 'World Map' leaves alone and the one left behind can't Start; the Host can't land while the Client
           is in the menu; a Proceed goes straight in when the other is already there; the Gummi Editor stays free; both
           press Start and both flights start.
  nolist   the Client's save has Mission 1 only (KH2COOP_GUMMI_CLIENT_UNLOCKED=1): both press Start, no mission list.
  refused  the Client's save lacks Mission 2 (KH2COOP_GUMMI_CLIENT_UNLOCKED=5): the Host can't pick it, and can pick another.
  ex       an EX mission unlocked in the Host's copy only is refused, then in both it reaches both games; then the Host's
           game closes during the Client's hold, and the Client is free again.

Not covered: the `tiny` screen (needs an Original Blueprint ship and item 0xAC; save 96 has none).
Everything is read from the logs and memory. Exit code 0 = every check passed. The real saves are never written (bench.py).
"""
import math
import os
import re
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench  # noqa: E402
from bench import Failed, wait  # noqa: E402
import peek  # noqa: E402
import test_gummi_door as door  # noqa: E402

from test_extra_copy import write_mem  # noqa: E402
from test_gummi_door import check, in_field, screen, q, logged  # noqa: E402

STACK, STACK_INDEX = 0xAF9E00, 0x50  # -> the screen stack; i32 current entry
MISSION = 0x716C00                   # u32: the mission the next flight flies
LAUNCH_FLAGS = 0xAFC0C4              # u32: bit 1 list up, bit 3 the launch has gone
FLIGHT_STATE = 0x729740 + 0xD0       # u32: 0 none, 1 starting, 2 flying
PLAYER, SHIP_TARGET, SHIP_COMMAND = 0x2A171C8, 0xDD0, 0xB42  # the ship; -> its target; u16 command on offer
CMD_LAND = 0xA3                      # the command the ship offers next to an open world: Land
LAND_SPOT = (7700.0, -7000.0)        # south of the Land of Dragons, where save 96 starts: Land is on offer here
EX_OPEN, EX_FLAG = 0x9BA06E, 0x9A878C  # u8: record (route 0, Mission 1, EX) +0x1E; the next flight is EX
LINK_GONE_S = 6                      # the mod calls the other game gone after 3 s of silence
MENU_S, LIST_S, RIDE_S, CHOICE_S = 4, 4, 7, 2.5  # the opening animations and the launch's ride up eat presses
SAVE, MAP = 96, (0x0F, 0x00)


def u32(c, off):
    return struct.unpack("<I", peek.read(c.game.h, c.game.base + off, 4))[0]


def stack(c):
    return struct.unpack("<i", peek.read(c.game.h, q(c, STACK) + STACK_INDEX, 4))[0]


def land_offered(c):
    target = c.game.q(q(c, PLAYER) + SHIP_TARGET)
    return bool(target) and struct.unpack("<H", peek.read(c.game.h, target + SHIP_COMMAND, 2))[0] == CMD_LAND


def tap(c, button, gap=0.8):
    c.game.pad.run(["tap", button])
    time.sleep(gap)


def marks(copies):
    return {c.name: c.mark() for c in copies}


def until(c, m, text, copies, timeout=12):
    wait(f"copy {c.name} to log '{text}'", lambda: logged(c, m[c.name], text), timeout, copies, step=0.1)


def said(c, m, text, copies):
    until(c, m, f"says '{text}'", copies)
    check(True, f"copy {c.name} shows '{text}'")


def not_said(c, m, text):
    check(not logged(c, m[c.name], f"says '{text}'"), f"copy {c.name} doesn't show '{text}'")


def on(copies, name, timeout=20):
    wait(f"both on '{name}'", lambda: all(screen(c) == name for c in copies), timeout, copies, step=0.1)


def enter(copies):
    bench.both(door.fly, copies)
    door.proceed(copies[0], copies)
    door.proceed(copies[1], copies)
    on(copies, "top", 40)
    check(not any(in_field(c) for c in copies), "both reached the Gummi menu")
    time.sleep(MENU_S)


def open_list(host, copies):
    tap(host, "cross")
    wait("the Host's mission list", lambda: screen(host) == "mission", 10, copies, step=0.1)
    time.sleep(LIST_S)


def pick(host, n):
    for _ in range(n - 1):
        tap(host, "right")
    tap(host, "cross")


def picked(host, m):
    """The mission of the last 'both ready' line."""
    found = [re.search(r"both ready: mission (\d)", line) for line in host.since(m["A"])]
    found = [f for f in found if f]
    return int(found[-1].group(1)) if found else None


def both_in_ships(host, copies, m, number=None):
    until(host, m, "both ready: mission", copies)
    on(copies, "body")
    n = picked(host, m)
    check(number is None or n == number, f"both ready on mission {n}")
    for c in copies:
        check(u32(c, MISSION) == n, f"copy {c.name} flies mission {u32(c, MISSION)}")
    time.sleep(MENU_S)


def to_launch(copies):
    for c in copies:
        tap(c, "cross", 0.1)
    on(copies, "lastcheck")
    time.sleep(RIDE_S)
    for c in copies:
        tap(c, "cross", 0.1)  # brings up Start / Abort
    time.sleep(CHOICE_S)


def scene_start_and_circle(host, friend, copies):
    print("--- the Client's Start waits; Circle cancels", flush=True)
    m = marks(copies)
    tap(friend, "cross")
    said(friend, m, "Waiting for Host to select a mission", copies)
    said(host, m, "Client ready for the mission", copies)
    check(screen(friend) == "top", "the Client stays on the top menu")
    m = marks(copies)
    tap(friend, "circle")
    until(friend, m, "our Start is called off", copies)
    check(screen(friend) == "top", "Circle cancelled and it's still on the top menu")


def scene_pick_first(host, friend, copies):
    print("--- the Host's pick waits; Circle cancels; the Client's Start sends both on", flush=True)
    open_list(host, copies)
    m = marks(copies)
    pick(host, 2)
    until(host, m, "our pick of mission 2 is held", copies)
    said(host, m, "Waiting for Client to Start", copies)
    said(friend, m, "Host selected mission", copies)
    m = marks(copies)
    tap(host, "circle")
    until(host, m, "our mission pick is called off", copies)
    check(screen(host) == "mission", "Circle cancelled and it's still on the mission list")
    m = marks(copies)
    tap(host, "cross")
    until(host, m, "our pick of mission 2 is held", copies)
    tap(friend, "cross")
    both_in_ships(host, copies, m, 2)
    check(stack(host) == 2 and stack(friend) == 1, f"the Client skipped its list (stacks {stack(host)}, {stack(friend)})")


def scene_circle_in_ships(leaver, other, copies):
    print(f"--- Circle in ship selection (copy {leaver.name}): both to the top menu", flush=True)
    m = marks(copies)
    tap(leaver, "circle")
    until(leaver, m, "our Circle in ship selection", copies)
    on(copies, "top")
    wait("both stacks at the top", lambda: all(stack(c) == 0 for c in copies), 5, copies, step=0.1)
    name = "Host" if leaver.name == "A" else "Client"
    said(other, m, f"{name} aborted", copies)
    time.sleep(1)
    not_said(leaver, m, f"{'Client' if name == 'Host' else 'Host'} aborted")
    time.sleep(MENU_S)


def scene_client_first(host, friend, copies):
    print("--- the Client waits first, then the Host picks", flush=True)
    m = marks(copies)
    tap(friend, "cross")
    said(host, m, "Client ready for the mission", copies)
    open_list(host, copies)
    m = marks(copies)
    pick(host, 1)
    both_in_ships(host, copies, m)


def scene_abort(starter, aborter, copies, with_circle):
    how = "Circle" if with_circle else "the Abort button"
    print(f"--- one Start waits, copy {aborter.name} aborts with {how}: both back", flush=True)
    to_launch(copies)
    m = marks(copies)
    tap(starter, "cross")
    until(starter, m, "our launch Start is held", copies)
    name = "Host" if starter.name == "A" else "Client"
    said(aborter, m, f"{name} is ready!", copies)
    check(not u32(starter, LAUNCH_FLAGS) & 1, "the held Start doesn't launch")
    m = marks(copies)
    if with_circle:
        tap(aborter, "circle")
    else:
        tap(aborter, "down")
        tap(aborter, "cross")
    until(aborter, m, "our Abort: both go back", copies)
    on(copies, "body")
    other = "Client" if aborter.name == "B" else "Host"
    said(starter, m, f"{other} aborted", copies)
    time.sleep(1)
    not_said(aborter, m, f"{name} aborted")
    check(not logged(aborter, m[aborter.name], "the other player aborted"), "the Abort isn't echoed back to who pressed it")
    time.sleep(MENU_S)


def scene_leave(host, friend, copies):
    print("--- 'World Map' leaves alone; the one left can't Start", flush=True)
    m = marks(copies)
    door.leave(host, copies)
    said(friend, m, "Host wants to leave the mission", copies)
    check(screen(friend) == "top", "the Client stays in the Gummi menu")
    m = marks(copies)
    tap(friend, "cross")
    said(friend, m, "Host isn't in the Gummi menu", copies)
    check(screen(friend) == "top", "the Client's Start is refused")


def scene_landing(host, friend, copies):
    print("--- the Host can't land while the Client is in the menu", flush=True)
    g, end = host.game, time.time() + 40
    while not land_offered(host):
        if time.time() > end:
            raise Failed(f"the Host's ship was never offered Land ({g.where()})")
        s = g.sora()
        dx, dz = LAND_SPOT[0] - s[0], LAND_SPOT[1] - s[2]
        d = math.hypot(dx, dz) or 1
        k = min(1.0, max(0.3, d / 800))
        g.pad.send(lx=k * dx / d, ly=k * dz / d, ms=40)
    g.pad.send(ms=300)
    m = marks(copies)
    tap(host, "triangle", 1.5)
    until(host, m, "landing refused", copies)
    check(in_field(host), "the Host stays on the map")
    check(bool(door.info(host)), "the INFO bar says why")
    check(screen(friend) == "top", "the Client stays in the Gummi menu")


def scene_straight_in(host, friend, copies):
    print("--- a Proceed goes straight in when the other is in that menu", flush=True)
    door.fly(host)
    m = marks(copies)
    door.proceed(host, copies)
    until(host, m, "straight in", copies)
    on(copies, "top", 40)
    time.sleep(MENU_S)


def scene_editor(host, friend, copies):
    print("--- the Gummi Editor is free: the message reaches the Garage", flush=True)
    tap(host, "down")
    tap(host, "cross", 2)
    wait("the Host's Garage", lambda: screen(host) == "fbody", 10, copies, step=0.1)
    m = marks(copies)
    tap(friend, "cross")
    said(friend, m, "Waiting for Host to select a mission", copies)
    said(host, m, "Client ready for the mission", copies)
    check(screen(host) == "fbody", "the Host's Garage isn't disturbed")
    m = marks(copies)
    tap(friend, "circle")
    until(friend, m, "our Start is called off", copies)
    tap(host, "circle")
    wait("the Host back on the top menu", lambda: screen(host) == "top", 10, copies, step=0.1)
    time.sleep(1)
    tap(host, "up")  # the cursor came back on Gummi Editor


def scene_launch(host, friend, copies):
    print("--- both press Start: both flights start", flush=True)
    m = marks(copies)
    tap(friend, "cross")
    said(host, m, "Client ready for the mission", copies)
    open_list(host, copies)
    m = marks(copies)
    pick(host, 1)
    both_in_ships(host, copies, m)
    to_launch(copies)
    m = marks(copies)
    tap(host, "cross")
    until(host, m, "our launch Start is held", copies)
    said(host, m, "Waiting for Client to Start", copies)
    said(friend, m, "Host is ready!", copies)
    m = marks(copies)
    tap(friend, "cross")
    for c in copies:
        until(c, m, "both pressed Start: the launch goes", copies)
    wait("both launches to finish", lambda: all(u32(c, LAUNCH_FLAGS) & 8 for c in copies), 25, copies, step=0.2)
    wait("both flights to start", lambda: all(u32(c, FLIGHT_STATE) in (1, 2) for c in copies), 30, copies, step=0.2)
    check(True, "both flights started")


def run_main(copies):
    host, friend = copies
    enter(copies)
    scene_start_and_circle(host, friend, copies)
    scene_pick_first(host, friend, copies)
    scene_circle_in_ships(friend, host, copies)
    scene_client_first(host, friend, copies)
    scene_abort(host, friend, copies, False)
    scene_abort(friend, host, copies, True)
    scene_circle_in_ships(host, friend, copies)
    scene_leave(host, friend, copies)
    scene_landing(host, friend, copies)
    scene_straight_in(host, friend, copies)
    scene_editor(host, friend, copies)
    scene_launch(host, friend, copies)


def run_nolist(copies):
    host, friend = copies
    enter(copies)
    print("--- no mission list for both: each Start waits for the other's", flush=True)
    m = marks(copies)
    tap(host, "cross")
    until(host, m, "our Start is held (no list for both)", copies)
    said(host, m, "Waiting for Client to Start", copies)
    said(friend, m, "Host ready for the mission", copies)
    check(screen(host) == "top", "the Host stays on the top menu, no list")
    m = marks(copies)
    tap(host, "circle")
    until(host, m, "our Start is called off", copies)
    m = marks(copies)
    tap(friend, "cross")
    said(friend, m, "Waiting for Host to Start", copies)
    tap(host, "cross")
    until(host, m, "both ready: mission 1 for both", copies)
    on(copies, "body")
    for c in copies:
        check(stack(c) == 1, f"copy {c.name} went straight to ship selection (stack {stack(c)})")
        check(u32(c, MISSION) == 1, f"copy {c.name} flies mission 1")


def run_refused(copies):
    host, friend = copies
    enter(copies)
    print("--- the Host can't pick a mission the Client hasn't unlocked", flush=True)
    tap(friend, "cross")
    open_list(host, copies)
    m = marks(copies)
    pick(host, 2)
    until(host, m, "refused: the other save doesn't have it", copies)
    said(host, m, "Client hasn't unlocked this mission", copies)
    check(screen(host) == "mission", "the Host stays on the list")
    m = marks(copies)
    tap(host, "left")
    tap(host, "cross")
    both_in_ships(host, copies, m, 1)


def run_ex(copies):
    host, friend = copies
    write_mem(host, host.game.base + EX_OPEN, b"\1")  # this bench copy's memory only
    enter(copies)
    print("--- an EX pick: only when both saves have it, and it reaches both", flush=True)
    open_list(host, copies)
    tap(host, "triangle", 1.5)
    m = marks(copies)
    tap(host, "cross")
    until(host, m, "pick of mission 1 EX refused", copies)
    said(host, m, "Client hasn't unlocked this mission", copies)
    write_mem(friend, friend.game.base + EX_OPEN, b"\1")
    time.sleep(1)  # the next status packets carry it
    tap(friend, "cross")
    m = marks(copies)
    tap(host, "cross")
    both_in_ships(host, copies, m, 1)
    for c in copies:
        check(peek.read(c.game.h, c.game.base + EX_FLAG, 1)[0] == 1, f"copy {c.name} flies the EX mission")
    scene_circle_in_ships(friend, host, copies)
    print("--- the other game closes during a hold: the one left is free again", flush=True)
    m = marks(copies)
    tap(friend, "cross")
    until(friend, m, "our Start is held", copies)
    host.proc.kill()
    time.sleep(LINK_GONE_S)
    tap(friend, "cross")
    wait("the Client's Start to work alone", lambda: screen(friend) in ("mission", "body"), 10, [friend], step=0.1)
    check(True, f"the hold ended and Start works alone ({screen(friend)})")


RUNS = {"main": (run_main, []), "nolist": (run_nolist, ["KH2COOP_GUMMI_CLIENT_UNLOCKED=1"]),
        "refused": (run_refused, ["KH2COOP_GUMMI_CLIENT_UNLOCKED=5"]), "ex": (run_ex, [])}


def main():
    for name in sys.argv[1:] or RUNS:
        fn, extra = RUNS[name]
        print(f"=== {name}", flush=True)
        copies = bench.launch(extra)
        print(f"pids: host {copies[0].proc.pid}, friend {copies[1].proc.pid}", flush=True)
        try:
            bench.load_slot_both(copies, SAVE, MAP, "the World Map")
            fn(copies)
            for c in copies:
                check(not c.crash, f"copy {c.name}: no crash")
        except Failed as e:
            check(False, str(e))
        finally:
            bench.close(copies)
    print("PASS" if not door.FAILS else f"FAILED, {len(door.FAILS)}: " + "; ".join(door.FAILS))
    sys.exit(1 if door.FAILS else 0)


if __name__ == "__main__":
    main()
