"""Bench for the Gummi flight in co-op (TODO 5.2, stage 1 item 5 of docs/GUMMI_COOP.md): save 96, route 0, Mission 1.

Usage: py tools/test_gummi_flight.py [main] [down] [lost]     (default: all three; each starts its own pair)

  main  ships locked at full HP. The tips window waits for both, in each order, and the two flights start together; a pause
        by either pauses both, the other's list is inert, closing reaches both; a Retry from the pause restarts both; the
        first at the results list waits; then only the Host's list responds: Retry restarts both, World Map takes both back.
  down  mortal ships (KH2COOP_GOD=0), nobody steering. Both shot down: the Host picks Retry for both. Then the Host's ship is
        kept alive from outside and the Client quits through the pause: that counts as shot down, it waits on the Game
        Over list until the Host has finished, the Host's clear counts in the Client's save (its record is wiped first, in
        memory only), and the Host picks Gummi Menu for both.
  lost  the Host's game closes while the Client is held at the tips: the Client's flight starts alone.

Everything is read from the logs and memory. Exit code 0 = every check passed. The real saves are never written (bench.py).
"""
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench  # noqa: E402
from bench import Failed, wait  # noqa: E402
import peek  # noqa: E402
import test_gummi_door as door  # noqa: E402
import test_gummi_menu as gummi  # noqa: E402

from test_extra_copy import write_mem  # noqa: E402
from test_gummi_door import check, in_field, screen, q, logged  # noqa: E402
from test_gummi_menu import marks, tap, until  # noqa: E402

SHIP, SHIP_HP, SHIP_MAX, SHIP_STATE = 0xAF0540, 0x578, 0x57C, 0x5D8  # -> gb::PLAYER; f32; i32; i32
STARTING, FLYING, GOAL, SHOT_DOWN = 1, 2, 3, 4
OPEN_MENU, NO_MENU, PAUSE, GAME_OVER, RESULTS = 0x7435D0, 0xFF, 0x0B, 0x0C, 0x0D
PROGRESS = 0x729740 + 0x14            # f32: the flight's rail progress
RETRY, GUMMI_MENU, WORLD_MAP = 0, 1, 2  # the items of the Game Over and results lists
PAUSE_GUMMI_MENU = 7                  # that item's place in the pause list
RECORDS, RECORD_SIZE, REC_CLEARED, REC_OPEN = 0x9B9990, 0x40, 0x1D, 0x1E  # the save's mission records; route 0 Mission 1 first
FLIGHT_S, TALLY_S, OPEN_S = 260, 80, 2  # a mission to its goal; the tally until it takes a Cross; a menu's opening
START_GAP_S = 1.0                     # the most the two flights' starts may differ on one PC
LINK_GONE_S = 6                       # the mod calls the other game gone after 3 s of silence


def ship(c):
    return q(c, SHIP)


def i32(c, addr):
    return struct.unpack("<i", peek.read(c.game.h, addr, 4))[0]


def state(c):
    p = ship(c)
    return i32(c, p + SHIP_STATE) if p else 0


def menu(c):
    return peek.read(c.game.h, c.game.base + OPEN_MENU, 1)[0]


def progress(c):
    return struct.unpack("<f", peek.read(c.game.h, c.game.base + PROGRESS, 4))[0]


def row(c):
    return f"{c.name}: ship state {state(c)}, menu {menu(c):02X}, progress {progress(c):.0f}"


def press_until(c, done, what, copies, timeout=30, gap=1.0):
    """Cross on this copy until done(); the windows here ignore a press until their timers allow it."""
    end = time.time() + timeout
    while not done():
        if time.time() > end or any(x.crash for x in copies):
            raise Failed(f"copy {c.name}: Cross never reached {what} ({row(c)})")
        tap(c, "cross", gap)


def both_menu(copies, value, what, timeout=10):
    wait(what, lambda: all(menu(c) == value for c in copies), timeout, copies, step=0.1)


def scene_tips(first, second, copies):
    print(f"--- the tips: copy {first.name} presses first and waits", flush=True)
    m = marks(copies)
    press_until(first, lambda: logged(first, m[first.name], "our press at the tips is held"), "the tips' hold", copies)
    time.sleep(4)
    check(state(first) == STARTING, f"copy {first.name} is still at the tips after pressing ({row(first)})")
    press_until(second, lambda: logged(second, m[second.name], "both pressed at the tips"), "both pressed", copies)
    started, end = {}, time.time() + 20
    while len(started) < 2 and time.time() < end:
        for c in copies:
            if c.name not in started and state(c) == FLYING:
                started[c.name] = time.time()
        time.sleep(0.02)
    check(len(started) == 2, f"both flights started ({row(first)}; {row(second)})")
    if len(started) == 2:
        gap = abs(started["A"] - started["B"])
        check(gap <= START_GAP_S, f"the two flights started {gap:.2f} s apart")


def scene_pause(owner, follower, copies):
    print(f"--- copy {owner.name} pauses: both pause, only it uses the list", flush=True)
    m = marks(copies)
    tap(owner, "start", 0.5)
    both_menu(copies, PAUSE, "both paused")
    until(follower, m, "opened our combat pause to match", copies)
    check(True, "one pause paused both")
    time.sleep(OPEN_S)
    before = [progress(c) for c in copies]
    for button in ("cross", "circle", "start"):
        tap(follower, button, 1)
    check(menu(follower) == PAUSE, f"the follower's buttons don't close its pause ({row(follower)})")
    check(before == [progress(c) for c in copies], "the rail stands still in both games")
    m = marks(copies)
    tap(owner, "start", 0.5)  # Circle isn't bound in this list
    both_menu(copies, NO_MENU, "both unpaused")
    until(follower, m, "closed our combat pause to match", copies)
    check(True, "the close reached both")
    time.sleep(2)
    check(all(state(c) == FLYING for c in copies), "both fly on")


def restarted(copies, why):
    wait(why, lambda: all(state(c) == STARTING and menu(c) == NO_MENU for c in copies), 25, copies, step=0.1)
    check(True, why)


def scene_pause_retry(owner, follower, copies):
    print(f"--- copy {owner.name} picks Retry in the pause: both restart", flush=True)
    m = marks(copies)
    tap(owner, "start", 0.5)
    both_menu(copies, PAUSE, "both paused")
    time.sleep(OPEN_S)
    tap(owner, "down", 0.6)
    tap(owner, "cross", 0.3)
    until(owner, m, "our Retry from the pause", copies)
    restarted(copies, "the pause's Retry restarted both")


def pick(host, friend, copies, item):
    m = marks(copies)
    for _ in range(item):
        tap(host, "down", 0.6)
    tap(host, "cross", 0.3)
    until(host, m, f"the Host's pick is item {item}", copies)
    until(friend, m, f"the Host picked item {item}", copies)


def client_inert(friend):
    time.sleep(OPEN_S)
    before = menu(friend)
    tap(friend, "cross", 2)
    check(menu(friend) == before and ship(friend) != 0, f"the Client's list doesn't respond ({row(friend)})")


def scene_goal(first, second, copies):
    host, friend = copies
    print(f"--- the goal: copy {first.name} is first at the list and waits", flush=True)
    wait("both ships at the goal", lambda: all(state(c) == GOAL for c in copies), FLIGHT_S, copies, step=0.5)
    m = marks(copies)
    press_until(first, lambda: menu(first) == RESULTS, "the results list", copies, TALLY_S, 1.5)
    until(first, m, "held until the other player is there too", copies)
    time.sleep(OPEN_S)
    tap(first, "cross", 2)
    check(menu(first) == RESULTS and state(first) == GOAL, f"the first there can't leave its list ({row(first)})")
    press_until(second, lambda: menu(second) == RESULTS, "the results list", copies, TALLY_S, 1.5)
    until(friend, m, "waiting for the Host's pick", copies)
    client_inert(friend)
    check(all(menu(c) == RESULTS for c in copies), "both still on the results list")


def run_main(copies):
    host, friend = copies
    gummi.enter(copies)
    gummi.scene_launch(host, friend, copies)
    scene_tips(host, friend, copies)
    scene_pause(host, friend, copies)
    scene_pause(friend, host, copies)
    scene_pause_retry(friend, host, copies)
    scene_tips(friend, host, copies)
    scene_goal(friend, host, copies)
    print("--- the Host's Retry restarts both", flush=True)
    pick(host, friend, copies, RETRY)
    restarted(copies, "the Host's Retry restarted both")
    scene_tips(host, friend, copies)
    scene_goal(host, friend, copies)
    print("--- the Host's World Map takes both back", flush=True)
    pick(host, friend, copies, WORLD_MAP)
    wait("both on the World Map", lambda: all(in_field(c) and not ship(c) for c in copies), 60, copies, step=0.2)
    check(True, "the Host's World Map took both to the map")


def keep_alive(c, done, what, copies, timeout=FLIGHT_S):
    end = time.time() + timeout
    while not done():
        if time.time() > end or any(x.crash for x in copies):
            raise Failed(f"{what} never happened ({row(copies[0])}; {row(copies[1])})")
        p = ship(c)
        if p:
            write_mem(c, p + SHIP_HP, struct.pack("<f", float(i32(c, p + SHIP_MAX))))
        time.sleep(0.25)


def forget_clear(c):
    """In this bench copy's memory only: route 0 Mission 1 not cleared, Missions 1 and 2 not open."""
    base = c.game.base + RECORDS
    write_mem(c, base + REC_CLEARED, b"\0")
    for n in range(2):
        write_mem(c, base + n * RECORD_SIZE + REC_OPEN, b"\0")


def clear_counted(c):
    base = c.game.base + RECORDS
    return (peek.read(c.game.h, base + REC_CLEARED, 1)[0] == 1 and
            all(peek.read(c.game.h, base + n * RECORD_SIZE + REC_OPEN, 1)[0] == 1 for n in range(2)))


def scene_quit(quitter, other, copies):
    m = marks(copies)
    tap(quitter, "start", 0.5)
    both_menu(copies, PAUSE, "both paused")
    time.sleep(OPEN_S)
    for _ in range(PAUSE_GUMMI_MENU):
        tap(quitter, "down", 0.4)
    tap(quitter, "cross", 0.3)
    until(quitter, m, "our quit from the pause counts as shot down", copies)
    both_menu(copies, NO_MENU, "both unpaused")
    wait("the quitter's ship to go down", lambda: state(quitter) == SHOT_DOWN, 10, copies, step=0.1)
    check(True, "the quit took the ship down")
    check(state(other) == FLYING, f"the other flies on ({row(other)})")


def run_down(copies):
    host, friend = copies
    gummi.enter(copies)
    gummi.scene_launch(host, friend, copies)
    scene_tips(host, friend, copies)
    print("--- both ships down: the Host picks for both", flush=True)
    m = marks(copies)
    wait("both ships shot down", lambda: all(state(c) == SHOT_DOWN for c in copies), FLIGHT_S, copies, step=0.5)
    both_menu(copies, GAME_OVER, "both on the Game Over list", 20)
    until(friend, m, "waiting for the Host's pick", copies)
    client_inert(friend)
    pick(host, friend, copies, RETRY)
    restarted(copies, "the Host's Retry restarted both")
    scene_tips(friend, host, copies)
    print("--- the Client quits through the pause and waits; the Host flies on and finishes", flush=True)
    forget_clear(friend)
    check(not clear_counted(friend), "the Client's record starts wiped")
    m = marks(copies)
    scene_quit(friend, host, copies)
    keep_alive(host, lambda: menu(friend) == GAME_OVER, "the Client's Game Over list", copies)
    keep_alive(host, lambda: logged(friend, m[friend.name], "held until the other player is there too"), "the Client's hold", copies, 10)
    tap(friend, "cross", 2)
    check(menu(friend) == GAME_OVER, f"the Client waits on its list ({row(friend)})")
    keep_alive(host, lambda: state(host) == GOAL, "the Host's goal", copies)
    press_until(host, lambda: menu(host) == RESULTS, "the results list", copies, TALLY_S, 1.5)
    until(friend, m, "waiting for the Host's pick", copies)
    until(friend, m, "the other player's clear counts here", copies)
    check(clear_counted(friend), "the Host's clear counted in the Client's save")
    client_inert(friend)
    pick(host, friend, copies, GUMMI_MENU)
    wait("both on the Gummi menu", lambda: all(screen(c) == "top" for c in copies), 40, copies, step=0.2)
    check(True, "the Host's Gummi Menu took both there")


def run_lost(copies):
    host, friend = copies
    gummi.enter(copies)
    gummi.scene_launch(host, friend, copies)
    print("--- the other game closes during the tips' hold: the one left flies alone", flush=True)
    m = marks(copies)
    press_until(friend, lambda: logged(friend, m[friend.name], "our press at the tips is held"), "the tips' hold", copies)
    host.proc.kill()
    time.sleep(LINK_GONE_S)
    press_until(friend, lambda: state(friend) == FLYING, "the flight alone", [friend])
    check(True, "the hold ended and the flight started alone")


RUNS = {"main": (run_main, []), "down": (run_down, ["KH2COOP_GOD=0"]), "lost": (run_lost, [])}


def main():
    for name in sys.argv[1:] or RUNS:
        fn, extra = RUNS[name]
        print(f"=== {name}", flush=True)
        copies = bench.launch(extra)
        print(f"pids: host {copies[0].proc.pid}, friend {copies[1].proc.pid}", flush=True)
        try:
            bench.load_slot_both(copies, gummi.SAVE, gummi.MAP, "the World Map")
            fn(copies)
            for c in copies:
                check(not c.crash or not c.alive(), f"copy {c.name}: no crash")
        except Failed as e:
            check(False, str(e))
            for c in copies:
                print(row(c), *[line[:150] for line in c.lines()[-12:]], sep="\n  ", flush=True)
        finally:
            bench.close(copies)
    print("PASS" if not door.FAILS else f"FAILED, {len(door.FAILS)}: " + "; ".join(door.FAILS))
    sys.exit(1 if door.FAILS else 0)


if __name__ == "__main__":
    main()
