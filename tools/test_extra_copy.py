"""Automated checks of the extra copy (KH2COOP_EXTRA_COPY=1) on a bench pair, one scenario at a time.

Usage: py tools/test_extra_copy.py <scenario> [--slot N] [--env KEY=VALUE ...]

Scenarios:
  ship     save 45, warp into the ship fight (0x12/0x16 program 72), the host attacks for a while: no crash.
           No Sora copy is made there (the room spawns its own Sora, P_EX100_SIDECAR, not the PLAYER
           placeholder); both ways of adding one crashed (bench 2026-10-12 07:03 and 07:06, VERIFIED_OFFSETS).
  alone    the friend's game closes: the host sees the link lost, picks 'play on with Donald and Goofy'
           (debug link 3), reloads a room (no copy expected); the friend comes back and loads the
           same save: co-op again, the copy back in both games after the next room load.
  downed   god mode off. The friend's Sora takes a lethal hit (debug damage): down, not dead; then
           the host's too: both down = Game Over in both games.
  forms    the host goes into Valor Form (command menu: Drive, first form), then Reverts: the player
           switches bodies and back, the Sora copy stays in both games, no crash; then a room reload
           makes the copy again.
  limit    the host opens Limit (the command menu's second page) and uses the first Limit with the first partner;
           screenshot after each step; no crash, the copy still there after it.
  summon   the same with Summon (the first summon in the list).
  party    the command menu's Party entry: swap the first companion for the first one offered (a world ally,
           e.g. Auron in Olympus: --slot 14); then a room reload. No crash, the copy stays.
  xemnas   save 45, warp into Final Xemnas (0x12/0x14 program 74; --ship: through the ship-fight room first, as
           in play), Xemnas's HP set to 200 in both games, the host hits him: both games reach his grab
           (scene re_b_ex170-50) and still run 20 s later (session 28 two-PC run: both crashed there at
           exe+0x3D76DF, old method). Riku and the Sora copy both expected in both games.
  xlimit   Final Xemnas as above, then the host opens Limit (second menu page) and uses the first one.
  menu     opens the command menu's Drive entry on the host and takes screenshots (to find the
           Drive Form / Summon entries for the next scenarios).
  hud      the third party gauge (party_hud.cpp): with Donald and Goofy both in the party the Sora copy's gauge
           shows above theirs; Valor Form (Goofy leaves): the copy moves into the freed gauge; Revert: back to the
           third gauge. Screenshots of the HUD corner at each step (hud_<copy>_<step>.png, *_crop.png enlarged).
  hud2     the third gauge in use (god mode off): the host opens the pause menu (the gauge dims with the HUD), the
           host's Sora takes 30 damage (the friend's third gauge drains), then the friend's copy in the host's game
           (forwarded: the friend's Sora loses it, the host's third gauge drains), a room change (04/09, the copy
           comes back into it), the host's Sora takes a lethal hit (downed: the KO face on the friend's).
           Screenshots hud_<copy>_<step>.
  item     TODO 1.46 (god mode off): the host uses its first item on itself twice (Items, first item, first
           target) at 10 HP. With --env KH2COOP_TEST_BAD_ITEM=1 the first use gets an item with no table row and
           the item guard (puppet.cpp) skips it; the second heals. (--env KH2COOP_ITEM_GUARD=0 too: the crash the
           guard prevents, at exe+0x3F6C94.)
  costume  TODO 5.6 (god mode off), --slot 17/21/22/23/25 (a world with its own Sora): the host attacks (shots of the
           copy on the friend's screen), then the friend's Sora goes down and gets up (shots of both screens).
  heal     TODO 1.52 / 1.58 (save 20, the Twin Lords fight, god mode for the party and the enemies, the companions'
           heal choice logged with KH2COOP_HEAL_TRACE=1): (1) the friend's Sora held at --low HP (debug lowhp): who
           sees it as needing help, who heals it; (2) the friend down (a 9999 hit): does a companion get it up;
           (3, 4) the same for the host's Sora (the control: the game's own heals). Screenshots heal_<phase>_<copy>_<n>,
           every trace and heal line in heal_<copy>.txt. PASS = no crash (the findings are in the summary).
Results: build/bench/extra_copy/<scenario>.txt and screenshots. PASS/FAIL exit code.
"""
import argparse
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench  # noqa: E402
import peek  # noqa: E402
import scene  # noqa: E402

OUT = os.path.join(bench.ROOT, "build", "bench", "extra_copy")


def find(lines, text):
    return [l for l in lines if text in l]


def copies_made(lines):
    return [l for l in lines if "extra copy:" in l and ": made" in l]


def ours(lines):
    after = [l for l in lines if "extra copy: after our Sora:" in l]
    return bool(after) and all(l.count("our Sora") == 4 for l in after)  # 'after our Sora' + 3x


def load_both(copies, slot, room):
    bench.load_slot_both(copies, slot, room)
    bench.wait("both in the save's room", lambda: all(c.room() == room for c in copies), 60, copies)
    time.sleep(6)


def ship(copies, a, out):
    load_both(copies, 45, (0x12, 0x12))
    marks = {c.name: c.mark() for c in copies}
    bench.warp(0x12, 0x16, 72)
    bench.wait("both in the ship fight", lambda: all(c.room() == (0x12, 0x16) and c.game.sora() for c in copies),
               90, copies)
    time.sleep(10)
    host = copies[0]
    for _ in range(30):  # the host attacks for a while (the 07:03 crash came at the first attack)
        host.game.pad.run(["tap", "cross", "wait", "300"])
    time.sleep(10)
    ok = True
    for c in copies:
        new = c.since(marks[c.name])
        made = copies_made(new)
        good = c.alive() and not c.crash and c.room() == (0x12, 0x16)
        ok &= good
        out.append(f"{c.name}: copy made {len(made)} (0 expected), alive {c.alive()}, crash {c.crash or '-'}")
        bench.screenshot(c.hwnd, os.path.join(OUT, f"ship_{c.name}.png"))
    return ok


def alone(copies, a, out):
    host, friend = copies
    load_both(copies, a.slot, a.room)
    m = host.mark()
    friend.proc.kill()
    bench.wait("the host sees the link lost", lambda: find(host.since(m), "link watch: connection lost"), 20, [host])
    time.sleep(2)
    bench.debug("A", 10, 3)  # play on with Donald and Goofy
    bench.wait("playing on", lambda: find(host.since(m), "link watch: playing on with Donald and Goofy"), 20, [host])
    time.sleep(8)
    m2 = host.mark()
    bench.warp(*a.room)
    bench.wait("the host's room reloaded", lambda: find(host.since(m2), "debug warp: asked"), 30, [host])
    time.sleep(10)
    solo_new = host.since(m2)
    solo_ok = not copies_made(solo_new) and host.alive() and not host.crash
    out.append(f"alone: playing on, room reloaded: copy made {len(copies_made(solo_new))} (expected 0), "
               f"alive {host.alive()}, crash {host.crash or '-'}")
    bench.screenshot(host.hwnd, os.path.join(OUT, "alone_A_solo.png"))
    # the friend comes back
    m3 = host.mark()
    new = bench.launch(names=("B",), keep_running=True)[0]
    copies[1] = new
    scene.load_slot(new.game, a.slot, a.room)
    bench.wait("co-op again on the host", lambda: find(host.since(m3), "co-op again") or
               find(host.since(m3), "game is back"), 60, [host, new])
    time.sleep(5)
    if not copies_made(host.since(m3)):  # the copy comes with the next room load
        bench.warp(*a.room)
    bench.wait("the copy back in the host's game", lambda: copies_made(host.since(m3)), 60, [host, new])
    time.sleep(10)
    back = host.since(m3)
    fnew = new.since(0)
    back_ok = bool(copies_made(back)) and ours(back) and bool(copies_made(fnew)) and not host.crash and not new.crash
    out.append(f"rejoin: host copy made {len(copies_made(back))}, ours {ours(back)}, friend copy made "
               f"{len(copies_made(fnew))}, crash {host.crash or new.crash or '-'}")
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"alone_{c.name}_back.png"))
    return solo_ok and back_ok


def downed(copies, a, out):
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    bench.debug("A", 3, 9999)  # the friend's Sora: a lethal hit on its copy in the host's game, forwarded (the friend's game drops other damage on its own Sora)
    time.sleep(1)
    fl = friend.since(marks["B"])
    down = find(fl, "down instead of dead")
    out.append(f"friend lethal hit: {'down' if down else 'NOT down'} ({(down or ['-'])[0][13:120]})")
    # Donald while the friend lies down (two-PC run: the friend heard Donald call 'Sora!' over and over)
    donald = {c.name: bench.actor_by_model(c, "P_EX020") for c in copies}
    seq = {c.name: [] for c in copies}
    for _ in range(4):  # short: the host stands next to the friend, which revives it within ~6 s
        for c in copies:
            m = bench.motion(c, donald[c.name]) if donald[c.name] else None
            if not seq[c.name] or seq[c.name][-1] != m:
                seq[c.name].append(m)
        time.sleep(0.5)
    for c in copies:
        out.append(f"{c.name}: Donald's motions over 3 s while the friend is down: {seq[c.name]}")
    bench.debug("A", 0, 9999)  # now the host too: both down
    time.sleep(20)
    over = {c.name: find(c.since(marks[c.name]), "re_gameover") + find(c.since(marks[c.name]), "both players are down")
            for c in copies}
    for c in copies:
        out.append(f"{c.name}: game over lines {len(over[c.name])} ({(over[c.name] or ['-'])[0][13:110]}), "
                   f"alive {c.alive()}, crash {c.crash or '-'}")
        bench.screenshot(c.hwnd, os.path.join(OUT, f"downed_{c.name}.png"))
    return bool(down) and all(over.values()) and all(c.alive() and not c.crash for c in copies)


DRIVE = ["tap", "down", "wait", "300"] * 3  # Attack -> Magic -> Items -> Drive


def forms(copies, a, out):
    host, friend = copies
    load_both(copies, a.slot, a.room)
    p = friend if a.by == "B" else host  # who goes into the form
    marks = {c.name: c.mark() for c in copies}
    p.game.pad.run(DRIVE + ["tap", "cross", "wait", "800"] + ["tap", "down", "wait", "300"] * a.pick +
                      ["tap", "cross", "wait", "500"])  # Drive -> Valor (--pick 1..4: Wisdom, Limit, Master, Final)
    time.sleep(12)
    bench.screenshot(host.hwnd, os.path.join(OUT, "forms_A_valor.png"))
    bench.screenshot(friend.hwnd, os.path.join(OUT, "forms_B_valor.png"))
    hl = p.since(marks[p.name])
    went = find(hl, "the player switched actors")
    out.append(f"form screenshot: forms_A_valor.png; commands {[l[13:90] for l in hl if 'menu: command' in l]}")
    out.append(f"form {a.pick}: player switched {len(went)} ({(find(hl, 'command 0x6') or ['no command 0x6 line'])[0][13:100]})")
    p.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])  # Revert (or the forms list)
    time.sleep(10)
    hl = p.since(marks[p.name])
    back = find(hl, "the player switched actors")
    out.append(f"Revert: player switched {len(back)} in all ({(find(hl, 'command 0x5') or ['no command 0x5 line'])[0][13:100]})")
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"forms_{c.name}_revert.png"))
    lost = {c.name: find(c.since(marks[c.name]), "Sora copy lost") + find(c.since(marks[c.name]), "copy gone") for c in copies}
    m2 = {c.name: c.mark() for c in copies}
    bench.warp(*a.room)
    bench.wait("the host's room reloaded", lambda: find(host.since(m2["A"]), "debug warp: asked"), 30, [host])
    time.sleep(12)
    again = copies_made(host.since(m2["A"]))
    out.append(f"after a room reload: host copy made {len(again)}, ours {ours(host.since(m2['A']))}")
    for c in copies:
        out.append(f"{c.name}: copy lost lines {len(lost[c.name])}, alive {c.alive()}, crash {c.crash or '-'}")
    return (len(went) >= 1 and len(back) >= 2 and bool(again) and ours(host.since(m2["A"]))
            and all(c.alive() and not c.crash for c in copies))


def hud_shot(c, step):
    from PIL import Image
    path = os.path.join(OUT, f"hud_{c.name}_{step}.png")
    bench.screenshot(c.hwnd, path)
    try:
        im = Image.open(path)
        w, h = im.size
        im.crop((int(w * 0.62), int(h * 0.45), w, h)).resize((int(w * 0.38 * 2), int(h * 0.55 * 2))).save(
            path.replace(".png", "_crop.png"))
    except OSError:
        pass


def hud(copies, a, out):
    host, friend = copies
    marks = {c.name: c.mark() for c in copies}
    load_both(copies, a.slot, a.room)
    time.sleep(10)
    for c in copies:
        hud_shot(c, "1_party")
    p = friend if a.by == "B" else host
    p.game.pad.run(DRIVE + ["tap", "cross", "wait", "800"] + ["tap", "down", "wait", "300"] * a.pick +
                   ["tap", "cross", "wait", "500"])
    time.sleep(12)
    for c in copies:
        hud_shot(c, "2_form")
    p.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])  # Revert
    time.sleep(10)
    for c in copies:
        hud_shot(c, "3_revert")
    ok = True
    for c in copies:
        lines = [l for l in c.since(marks[c.name]) if " hud: " in l]
        built = find(lines, "third gauge built")
        third = find(lines, "shown in the third gauge")
        moved = find(lines, "moved from the third gauge")
        out.append(f"{c.name}: built {len(built)}, shown in the third gauge {len(third)}, moved out {len(moved)}, "
                   f"alive {c.alive()}, crash {c.crash or '-'}")
        for l in lines[:40]:
            out.append("   " + l[:200])
        ok = ok and bool(built) and bool(third) and c.alive() and not c.crash
    return ok


def hud2(copies, a, out):
    host, friend = copies
    marks = {c.name: c.mark() for c in copies}
    load_both(copies, a.slot, a.room)
    time.sleep(10)
    for c in copies:
        hud_shot(c, "1_start")
    host.game.pad.run(["tap", "start", "wait", "1500"])
    for c in copies:
        hud_shot(c, "2_pause")
    host.game.pad.run(["tap", "start", "wait", "1500"])
    bench.warp(0x04, 0x09)  # away from the save point (it refills HP within a second)
    bench.wait("both in 04/09", lambda: all(c.room() == (0x04, 0x09) for c in copies), 60, copies)
    time.sleep(12)
    for c in copies:
        hud_shot(c, "3_room")
    bench.debug("A", 0, 30)  # the host's Sora (a hit on the friend's own Sora there is dropped: the host owns it)
    time.sleep(1)  # one test command per frame
    bench.debug("A", 3, 20)  # the friend's copy in the host's game: forwarded to the friend's Sora
    time.sleep(1)
    for c in copies:
        hud_shot(c, "4_hurt")
    bench.debug("A", 0, 999)
    time.sleep(3)
    for c in copies:
        hud_shot(c, "5_down")
    time.sleep(8)
    ok = True
    for c in copies:
        lines = [l for l in c.since(marks[c.name]) if " hud: " in l]
        third = find(lines, "shown in the third gauge")
        out.append(f"{c.name}: shown in the third gauge {len(third)}, HUD builds {len(find(lines, 'built its HUD'))}, "
                   f"alive {c.alive()}, crash {c.crash or '-'}")
        for l in lines[:40]:
            out.append("   " + l[:200])
        ok = ok and bool(third) and c.alive() and not c.crash
    return ok


PAGE2 = DRIVE + ["tap", "left", "wait", "1000"]  # Attack / Summon / Party / Limit, cursor on Limit


def special(copies, a, out, name, keys, picks=None, warp=None, steps=4):
    load_both(copies, a.slot, a.room)
    if warp:  # (world, room, program): e.g. into a fight
        bench.warp(*warp)
        bench.wait(f"both in {warp[0]:02X}/{warp[1]:02X}", lambda: all(c.room() == warp[:2] for c in copies), 60, copies)
        time.sleep(12)
    host = copies[1] if a.by == "B" else copies[0]  # the one who presses
    marks = {c.name: c.mark() for c in copies}
    host.game.pad.run(PAGE2 + keys)
    for step in range(steps):
        time.sleep(2.5)
        for c in (copies if a.shots else [host]):  # --shots: the other game too (TODO 1.57: the Limit partner)
            bench.screenshot(c.hwnd, os.path.join(OUT, f"{name}_{c.name}_{step}.png"))
        if step == 1 and picks:
            host.game.pad.run(picks)  # move down the opened list
        host.game.pad.run(["tap", "cross", "wait", "300"])
    if a.shots:
        for k in range(6):
            time.sleep(2)
            for c in copies:
                bench.screenshot(c.hwnd, os.path.join(OUT, f"{name}_{c.name}_{4 + k}.png"))
    time.sleep(8 if a.shots else 20)
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"{name}_{c.name}_after.png"))
    hl = host.since(marks[host.name])
    cmds = [l[13:110] for l in hl if "carries out command" in l or "menu: command" in l]
    out.append(f"host commands: {cmds[:8]}")
    for c in copies:
        gone = find(c.since(marks[c.name]), "Sora copy gone")
        out.append(f"{c.name}: copy gone lines {len(gone)} ({(gone or ['-'])[0][13:110]}), alive {c.alive()}, crash {c.crash or '-'}")
    return all(c.alive() and not c.crash for c in copies)


def limit(copies, a, out):
    marks = {c.name: c.mark() for c in copies}
    # A Limit only starts in a fight: on save 52's Borough save point and on the street next to it the Limit list
    # never opened (session 32, 'host commands: []' in every earlier run). Save 20 -> the Twin Lords fight.
    # 6 presses: a hit on the presser's Sora can swallow one (the Lords go for the host's).
    ok = special(copies, a, out, "limit", [], warp=(0x07, 0x03, 59) if a.slot == 20 else None, steps=6)
    user, other = (copies[1], copies[0]) if a.by == "B" else (copies[0], copies[1])
    started = find(user.since(marks[user.name]), "type 0x51)")  # a Limit command
    out.append(f"{user.name}: Limit commands {len(started)}")
    ok = ok and bool(started)
    if a.by == "B":  # TODO 1.57: the host's companion plays the friend's Limit partner
        lines = other.since(marks[other.name])
        borrowed = find(lines, "plays the friend's Limit partner")
        back = find(lines, "is back with our Sora")
        out.append(f"{other.name}: partner borrowed {len(borrowed)}, back {len(back)} ({(back or ['-'])[0][13:140]})")
        ok = ok and bool(borrowed)
    return ok


def summon(copies, a, out):
    ok = special(copies, a, out, "summon", ["tap", "up", "wait", "500", "tap", "up", "wait", "500"],
                 ["tap", "down", "wait", "300"] * a.pick)
    host = copies[1] if a.by == "B" else copies[0]
    m = host.mark()
    host.game.pad.run(["tap", "down", "wait", "500", "tap", "cross", "wait", "500"])  # Dismiss (second entry)
    time.sleep(15)
    bench.screenshot(host.hwnd, os.path.join(OUT, f"summon_{host.name}_dismissed.png"))
    hl = host.since(m)
    built = [l[13:80] for l in hl if "build member" in l]
    back = {n: bool(bench.actor_by_model(host, n)) for n in ("P_EX020", "P_EX030")}
    out.append(f"dismiss: members rebuilt {built}, Donald/Goofy back {back}, alive {host.alive()}, crash {host.crash or '-'}")
    return ok and all(back.values()) and all(c.alive() and not c.crash for c in copies)


def party(copies, a, out):
    host = copies[0]
    ok = special(copies, a, out, "party", ["tap", "up", "wait", "500"])  # Party -> FRIENDS -> first -> first offered
    hl = host.since(0)
    out.append(f"members built: {[l[13:80] for l in hl if 'build member' in l][:6]}")
    m = host.mark()
    bench.warp(*a.room)
    bench.wait("the host's room reloaded", lambda: find(host.since(m), "debug warp: asked"), 30, [host])
    time.sleep(12)
    again = copies_made(host.since(m))
    out.append(f"after a room reload: host copy made {len(again)}, ours {ours(host.since(m))}, "
               f"crash {host.crash or copies[1].crash or '-'}")
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"party_{c.name}_reload.png"))
    return ok and bool(again) and all(c.alive() and not c.crash for c in copies)


FINAL = (0x12, 0x14, 74)
GRAB = "re_b_ex170-50"


def to_final(copies, a, out):
    load_both(copies, 45, (0x12, 0x12))
    marks = {c.name: c.mark() for c in copies}
    if a.ship:
        bench.warp(0x12, 0x16, 72)
        bench.wait("both in the ship fight", lambda: all(c.room() == (0x12, 0x16) and c.game.sora() for c in copies),
                   90, copies)
        time.sleep(4)
    bench.warp(*FINAL)
    bench.wait("both in Final Xemnas", lambda: all(c.room() == FINAL[:2] and c.game.sora() for c in copies), 90, copies)
    time.sleep(8)  # the room's opening scenes
    for c in copies:
        new = c.since(marks[c.name])
        riku = bool(bench.actor_by_model(c, "P_EH000"))
        out.append(f"{c.name}: copy made {len(copies_made(new))}, ours {ours(new)}, puppet found "
                   f"{bool(find(new, 'puppet: Sora copy found'))}, Riku {riku}")
    return marks


class HpKeeper:
    """--hp: keeps the party's HP full by writing it (stat slots exe+0x2A23598 - k*0x278, k = 0..2: current =
    max) every 0.2 s, so a fight runs on without god mode (GOD=0): the hits land in full, only the HP comes back."""

    def __init__(self, copies):
        import ctypes
        import threading
        self.k32, self.stop_flag, self.handles = ctypes.windll.kernel32, False, []
        self.k32.OpenProcess.restype = ctypes.c_void_p
        for c in copies:
            h = self.k32.OpenProcess(0x0008 | 0x0010 | 0x0020, False, c.game.pid)  # VM_OPERATION | READ | WRITE
            self.handles.append((ctypes.c_void_p(h), c.game.base))
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        import ctypes
        buf, done = ctypes.create_string_buffer(8), ctypes.c_size_t()
        while not self.stop_flag:
            for h, base in self.handles:
                for k in range(3):
                    slot = ctypes.c_void_p(base + 0x2A23598 - k * 0x278)
                    if not self.k32.ReadProcessMemory(h, slot, buf, 8, ctypes.byref(done)):
                        continue
                    cur, mx = struct.unpack("<II", buf.raw)
                    if 0 < cur < mx <= 9999:
                        self.k32.WriteProcessMemory(h, slot, struct.pack("<I", mx), 4, ctypes.byref(done))
            time.sleep(0.2)

    def stop(self):
        self.stop_flag = True
        self.thread.join(2)


class MotionWatch:
    """--motions: every motion change (actor+0x180) of each Final Xemnas (B_EX170*) in both games, with the time."""

    def __init__(self, copies):
        import threading
        self.copies, self.stop_flag, self.t0 = copies, False, time.time()
        self.changes = {c.name: [] for c in copies}  # (t, actor, motion)
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        found, last, refresh = {c.name: [] for c in self.copies}, {}, 0
        while not self.stop_flag:
            if time.time() > refresh:
                for c in self.copies:
                    try:
                        found[c.name] = [x for x, n in bench.actors(c) if n.startswith("B_EX170")]
                    except OSError:
                        pass
                refresh = time.time() + 2
            for c in self.copies:
                for x in found[c.name]:
                    m = bench.motion(c, x)
                    if m is not None and last.get((c.name, x)) != m:
                        last[(c.name, x)] = m
                        self.changes[c.name].append((time.time() - self.t0, x, m))
            time.sleep(0.2)

    def stop(self):
        self.stop_flag = True
        self.thread.join(2)


def xemnas(copies, a, out):
    host = copies[0]
    pools = bench.PoolWatch(copies)
    keeper = HpKeeper(copies) if a.hp else None
    motions = MotionWatch(copies) if a.motions else None
    census = bench.FxCensus(copies) if a.fx_census else None
    try:
        return xemnas_run(copies, a, out)
    finally:
        if keeper:
            keeper.stop()
        if motions:
            motions.stop()
            with open(os.path.join(OUT, "xemnas_motions.txt"), "w") as fh:
                for c in copies:
                    fh.writelines(f"{c.name} {t:6.1f} {x:#x} {m}\n" for t, x, m in motions.changes[c.name])
        pools.stop()
        for c in copies:
            out.append(f"{c.name}: effect pool {pools.summary(c.name)}")
        if census:
            census.stop()
            for c in copies:
                out.extend(f"{c.name}: {line}" for line in census.summary(c.name))
            census.write(os.path.join(OUT, "xemnas_fx_census.csv"))
        with open(os.path.join(OUT, "xemnas_pool.csv"), "w") as fh:
            for c in copies:
                fh.writelines(f"{c.name},{t:.1f},{v}\n" for t, v in pools.samples[c.name])


def xemnas_run(copies, a, out):
    host = copies[0]
    marks = to_final(copies, a, out)
    hp_mark = host.mark()
    for c in copies:
        bench.debug(c.name, 13, 200)  # Xemnas's HP
    hit = lambda: any("HP change: other" in l and "team 2" in l for l in host.since(hp_mark))
    start, end = time.time(), time.time() + 60
    while time.time() < end and (not hit() or time.time() - start < 20):  # at least 20 s of attacks
        host.game.pad.run(["tap", "cross", "wait", "250"] * 4)
    out.append(f"a hit on Xemnas: {'yes' if hit() else 'none in 60 s'}")
    grabbed = lambda c: any(GRAB in l and "scene log" in l for l in c.since(marks[c.name]))
    end, shot = time.time() + 150, 0
    while time.time() < end and not all(grabbed(c) or c.crash or not c.alive() for c in copies):
        if a.shots and shot < 12:  # what Xemnas does in the next ~36 s
            for c in copies:
                if c.alive():
                    bench.screenshot(c.hwnd, os.path.join(OUT, f"xemnas_{c.name}_t{shot:02d}.png"))
            shot += 1
            time.sleep(2)
        time.sleep(1)
    if any(grabbed(c) for c in copies):
        time.sleep(20)
    for c in copies:
        if c.alive():
            bench.screenshot(c.hwnd, os.path.join(OUT, f"xemnas_{c.name}.png"))
        out.append(f"{c.name}: grab {grabbed(c)}, alive {c.alive()}, crash {c.crash or '-'}")
    return all(grabbed(c) and c.alive() and not c.crash for c in copies)


def boss_hp(c, actor):
    """HP of an actor (u32 at *(actor+0x5C0)), or None."""
    import peek
    try:
        stats = struct.unpack("<Q", peek.read(c.game.h, actor + 0x5C0, 8))[0]
        return struct.unpack("<i", peek.read(c.game.h, stats, 4))[0] if stats else None
    except OSError:
        return None


def xend(copies, a, out):
    """TODO 1.20 / 1.30: Final Xemnas to the end. Both Soras attack (cross) and take reaction commands (triangle);
    every boss's HP is set to --boss-hp at the start. Logs each game's scenes, Xemnas's HP and room side by side
    (xend_timeline.txt) until both games leave the room, one crashes, or --seconds pass."""
    pools = bench.PoolWatch(copies)
    keeper = HpKeeper(copies) if a.hp else None
    try:
        marks = to_final(copies, a, out)
        for c in copies:
            bench.debug(c.name, 13, a.boss_hp)
        t0, end, turn, sample = time.time(), time.time() + a.seconds, 0, 0
        timeline, bosses, left = [], {c.name: [] for c in copies}, None
        shots, next_shot = 0, 0
        while time.time() < end:
            if any(c.crash or not c.alive() for c in copies):
                break
            rooms = {c.name: c.room() for c in copies}
            if all(r != FINAL[:2] for r in rooms.values()):
                left = left or time.time()
                if time.time() - left > 25:
                    break
            if a.shots and shots < 24 and time.time() > next_shot and any(
                    "re_b_ex170-60" in l for l in copies[0].since(marks["A"])):
                for d in copies:  # what follows the scene, every 5 s
                    bench.screenshot(d.hwnd, os.path.join(OUT, f"xend_{d.name}_t{shots:02d}.png"))
                shots, next_shot = shots + 1, time.time() + 5
            c = copies[turn % 2]
            turn += 1
            if rooms[c.name] == FINAL[:2]:
                c.game.pad.run(["tap", "cross", "wait", "200", "tap", "triangle", "wait", "200"])
            if time.time() > sample:
                sample = time.time() + 2
                row = [f"{time.time() - t0:6.1f}"]
                for c in copies:
                    try:
                        bosses[c.name] = [(x, n) for x, n in bench.actors(c) if n.startswith("B_EX170")]
                    except OSError:
                        pass
                    hps = ",".join(f"{n[7:] or '-'}={boss_hp(c, x)}" for x, n in bosses[c.name])
                    row.append(f"{c.name} room {rooms[c.name][0]:02X}/{rooms[c.name][1]:02X} {hps}")
                timeline.append(" | ".join(row))
        for c in copies:
            lines = c.since(marks[c.name])
            scenes = [l[:12] + " " + l.split("cutscene '")[1].split("'")[0] for l in lines if "scene log: cutscene" in l]
            out.append(f"{c.name}: scenes {scenes}")
            out.append(f"{c.name}: room {c.room() if c.alive() else '-'}, alive {c.alive()}, crash {c.crash or '-'}, "
                       f"pool {pools.summary(c.name)}")
        with open(os.path.join(OUT, "xend_timeline.txt"), "w") as fh:
            fh.write("\n".join(timeline) + "\n")
        for c in copies:
            if c.alive():
                bench.screenshot(c.hwnd, os.path.join(OUT, f"xend_{c.name}.png"))
        return all(c.alive() and not c.crash for c in copies) and left is not None
    finally:
        pools.stop()
        if keeper:
            keeper.stop()


def xlimit(copies, a, out):
    host = copies[0]
    marks = to_final(copies, a, out)
    host.game.pad.run(PAGE2)
    for step in range(4):
        time.sleep(2)
        bench.screenshot(host.hwnd, os.path.join(OUT, f"xlimit_A_{step}.png"))
        host.game.pad.run(["tap", "cross", "wait", "300"])
    time.sleep(15)
    for c in copies:
        if c.alive():
            bench.screenshot(c.hwnd, os.path.join(OUT, f"xlimit_{c.name}_after.png"))
    hl = host.since(marks["A"])
    out.append(f"host: {[l[13:110] for l in hl if 'menu: command' in l or 'carries out command' in l or 'Limit' in l][:10]}")
    for c in copies:
        out.append(f"{c.name}: limit lines {[l[13:100] for l in c.since(marks[c.name]) if 'Limit' in l][:4]}, "
                   f"alive {c.alive()}, crash {c.crash or '-'}")
    return all(c.alive() and not c.crash for c in copies)


def read_u(c, addr, size=4):
    import peek
    return int.from_bytes(peek.read(c.game.h, addr, size), "little")


def xcontinue(copies, a, out):
    """TODO 1.10: both down in Final Xemnas -> Game Over -> the host picks Continue (the friend follows, game_over.cpp).
    Checks the in-field byte (exe+0x9BA8D0) drops to 0 in both games (= world sync counts a new room instance), the
    copy is made again and the friend's Xemnas is bound to the host's again."""
    import threading
    host, friend = copies
    to_final(copies, a, out)
    marks = {c.name: c.mark() for c in copies}
    bench.debug("A", 3, 9999)  # the friend down (a lethal hit on its copy, forwarded)
    time.sleep(1)
    bench.debug("A", 0, 9999)  # the host down: both down = Game Over
    menu_up = lambda c: (lambda go: go and read_u(c, go + 0x10) == 6)(read_u(c, c.game.base + 0x2AE8050, 8))
    bench.wait("the host's Game Over menu", lambda: menu_up(host), 40, copies)
    out.append(f"Game Over menu: host {menu_up(host)}, friend {menu_up(friend)}")
    time.sleep(2)  # the menu fades in
    field = {c.name: [] for c in copies}
    stop = []

    def sample():
        while not stop:
            for c in copies:
                try:
                    v = read_u(c, c.game.base + 0x9BA8D0, 1)
                except OSError:
                    continue
                if not field[c.name] or field[c.name][-1] != v:
                    field[c.name].append(v)
            time.sleep(0.05)
    t = threading.Thread(target=sample, daemon=True)
    t.start()
    cont = {c.name: c.mark() for c in copies}
    host.game.pad.run(["tap", "cross", "wait", "300"])  # Continue (the default cursor)
    bench.wait("both back in the field", lambda: all(len(field[c.name]) >= 3 and field[c.name][-1] == 1
                                                         for c in copies), 60, copies)
    time.sleep(12)
    stop.append(1)
    t.join(1)
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"xcontinue_{c.name}.png"))
    ok = True
    for c in copies:
        lines = c.since(cont[c.name])
        chose = [l[13:90] for l in lines if "game over:" in l][:3]
        bound = [l[13:100] for l in lines if "bound to ours" in l or "unbound" in l][:3]
        made = len(copies_made(lines))
        out.append(f"{c.name}: in-field values {field[c.name]}, game over lines {chose}, copy made {made}, "
                   f"world lines {bound}, room {c.room()}, alive {c.alive()}, crash {c.crash or '-'}")
        ok &= 0 in field[c.name] and field[c.name][-1] == 1 and made >= 1 and c.alive() and not c.crash
    fl = friend.since(cont["B"])
    ok &= any("bound to ours" in l for l in fl)
    return ok


def costume(copies, a, out):
    """TODO 5.6: in a world with its own Sora (--slot: 17 Timeless River, 21 Halloween Town, 22 Christmas Town, 23
    Pride Lands, 25 Space Paranoids): the host attacks (the copy on the friend's screen), then the friend's Sora goes
    down and gets up (the copy on the host's screen and the friend's own). Screenshots costume_<slot>_<step>_<copy>."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    time.sleep(4)

    def shot(step, who=copies):
        for c in who:
            bench.screenshot(c.hwnd, os.path.join(OUT, f"costume_{a.slot}_{step}_{c.name}.png"))

    shot("0_start")
    for i in range(3):
        host.game.pad.run(["tap", "cross", "wait", "180"])
        shot(f"1_attack{i}", [friend])
    time.sleep(2)
    bench.debug("A", 3, 9999)  # the friend's Sora: a lethal hit on its copy in the host's game, forwarded
    prev = 0
    for t in (1, 3, 5, 7, 9, 12):
        time.sleep(t - prev)
        prev = t
        shot(f"2_down{t:02d}s")
    fl = friend.since(marks["B"])
    down = find(fl, "down instead of dead")
    up = find(fl, "getting up")
    out.append(f"friend lethal hit: {'down' if down else 'NOT down'} ({(down or ['-'])[0][13:140]})")
    out.append(f"friend got up: {len(up)} ({(up or ['-'])[0][13:120]})")
    for c in copies:
        lines = c.since(marks[c.name])
        odd = [l[13:160] for l in lines if "motion" in l and ("not found" in l or "missing" in l or "no motion" in l)]
        out.append(f"{c.name}: motion problems logged {len(odd)} {odd[:3]}, alive {c.alive()}, crash {c.crash or '-'}")
    return bool(down) and all(c.alive() and not c.crash for c in copies)


def actor_info(c, a):
    """Model, character id (*(decode(actor+8)) + 0x4C), stat block (+0x5C0) with HP/max and which stat slot it is
    (exe+0x2A23598 - k * 0x278, k = 0 Sora), team, weapons (+0xD60 / +0xD68) models."""
    h, base = c.game.h, c.game.base
    q = lambda addr: struct.unpack("<Q", peek.read(h, addr, 8))[0]
    i32 = lambda addr: struct.unpack("<i", peek.read(h, addr, 4))[0]
    name = dict(bench.actors(c)).get(a, "?")
    rec = bench.decode(c, struct.unpack("<I", peek.read(h, a + 8, 4))[0])
    char = struct.unpack("<H", peek.read(h, rec + 0x4C, 2))[0] if rec else -1
    stats = q(a + 0x5C0)
    slot = next((k for k in range(-2, 12) if stats == base + 0x2A23598 - k * 0x278), None)
    hp = (i32(stats), i32(stats + 4)) if stats else None
    weapons = []
    for off in (0xD60, 0xD68):
        w = q(a + off)
        weapons.append(bench.model_of(c, w) if w else "-")
    return (f"{name} exe+0x{a - base:X}: character {char}, stats exe+0x{stats - base:X} (slot {slot}) HP {hp}, "
            f"team {i32(a + 0x4DC)}, weapons {weapons}")


def inspect(copies, a, out):
    """TODO 3.2: what the Sora copy uses in each game (character id, stat block, weapons), then the pause menu's
    pages on the host (screenshots inspect_menu_<n>.png)."""
    load_both(copies, a.slot, a.room)
    time.sleep(3)
    h = copies[0].game.h
    for c in copies:
        base = c.game.base
        sora = struct.unpack("<Q", peek.read(c.game.h, base + 0x2A171C8, 8))[0]
        party = [struct.unpack("<Q", peek.read(c.game.h, base + 0x2A239B0 + 8 * i, 8))[0] for i in range(2)]
        copy = next((x for x, n in bench.actors(c) if n.startswith("P_EX100") and x != sora), 0)
        out.append(f"{c.name}:")
        for who, x in (("our Sora", sora), ("companion 1", party[0]), ("companion 2", party[1]), ("Sora copy", copy)):
            if x:
                try:
                    out.append(f"   {who:12s} {actor_info(c, x)}")
                except OSError as e:
                    out.append(f"   {who:12s} read failed: {e}")
    host = copies[0]
    steps = [("pause", ["tap", "start", "wait", "1500"])] +             [(f"down{i}", ["tap", "down", "wait", "500"]) for i in range(1, 7)]
    for name, keys in steps:
        host.game.pad.run(keys)
        time.sleep(0.6)
        bench.screenshot(host.hwnd, os.path.join(OUT, f"inspect_menu_{name}.png"))
    return all(c.alive() and not c.crash for c in copies)


def sora_hp(c):
    """Our Sora's HP in that game, or None."""
    try:
        sora = struct.unpack("<Q", peek.read(c.game.h, c.game.base + 0x2A171C8, 8))[0]
        stats = struct.unpack("<Q", peek.read(c.game.h, sora + 0x5C0, 8))[0] if sora else 0
        return struct.unpack("<i", peek.read(c.game.h, stats, 4))[0] if stats else None
    except OSError:
        return None


def item(copies, a, out):
    """TODO 1.46: the host uses the first item on itself twice (Items, the first item, the first target). With
    KH2COOP_TEST_BAD_ITEM=1 the first use gets an item with no table row: the guard skips it (with
    KH2COOP_ITEM_GUARD=0 the game crashes at exe+0x3F6C94 ~0.5 s later); the second heals as normal."""
    host, friend = copies
    marks = {c.name: c.mark() for c in copies}
    load_both(copies, a.slot, a.room)
    bench.warp(0x04, 0x09)  # away from the save point (it refills HP within a second)
    bench.wait("both in 04/09", lambda: all(c.room() == (0x04, 0x09) for c in copies), 60, copies)
    time.sleep(12)
    use = ["tap", "down", "wait", "300"] * 2 + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "800",
                                                 "tap", "cross", "wait", "300"]
    ok = True
    for n in range(2):
        bench.debug("A", 0, 10)
        time.sleep(1.5)
        before = sora_hp(host)
        host.game.pad.run(use)
        time.sleep(4)
        bench.screenshot(host.hwnd, os.path.join(OUT, f"item_{n}_A.png"))
        after = sora_hp(host)
        out.append(f"use {n + 1}: host HP {before} -> {after}, alive A {host.alive()} B {friend.alive()}")
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                out.append(f"{c.name}: {c.crash or 'closed'}")
                return False
        if n == 1:
            ok &= after is not None and before is not None and after > before
    hl = [l for l in host.since(marks["A"]) if "item" in l.lower()]
    for l in hl[:20]:
        out.append("   " + l[:220])
    skipped = find(hl, "no item table row")
    test = any("KH2COOP_TEST_BAD_ITEM=1" in e for e in a.env)
    out.append(f"guard skips: {len(skipped)} (test bad item {'on' if test else 'off'})")
    ok &= bool(skipped) == test
    return ok and all(c.alive() and not c.crash for c in copies)


def heal(copies, a, out):
    host, friend = copies
    load_both(copies, a.slot, a.room)
    bench.warp(0x07, 0x03, 59)  # the Twin Lords fight: companions only weigh heals in a fight
    bench.wait("both in 07/03", lambda: all(c.room() == (0x07, 0x03) for c in copies), 60, copies)
    time.sleep(12)
    start = {c.name: c.mark() for c in copies}

    def friend_low():
        bench.debug("B", 16, a.low)
        time.sleep(0.5)
        bench.debug("A", 3, 1)  # a hit on the friend's copy, forwarded: the friend's Sora drops to the hold

    def friend_down():
        bench.debug("A", 3, 9999)

    def host_low():
        bench.debug("B", 16, 0)
        bench.debug("A", 16, a.low)
        time.sleep(0.5)
        bench.debug("A", 0, 1)

    def host_down():
        # never both down (= Game Over): wait for the friend's get-up first
        bench.wait("the friend up again", lambda: find(friend.since(start["B"]), "downed: getting up") or
                   find(friend.since(start["B"]), "reached our Sora: getting up"), 45, copies)
        time.sleep(3)
        bench.debug("A", 0, 9999)

    phases = [("friend_low", friend_low, 40), ("friend_down", friend_down, 36), ("host_low", host_low, 40),
              ("host_down", host_down, 36)]
    keys = ["heal trace:", "heal: ", "healed our Sora", "downed: ", "debug: low-HP", "the peer's hit"]
    for name, act, secs in phases:
        marks = {c.name: c.mark() for c in copies}
        act()
        hp = {c.name: [] for c in copies}
        for k in range(secs // 2):
            time.sleep(2)
            for c in copies:
                hp[c.name].append(sora_hp(c))
                if k % 3 == 0:
                    bench.screenshot(c.hwnd, os.path.join(OUT, f"heal_{name}_{c.name}_{k // 3}.png"))
            for c in copies:
                c.lines()
                if c.crash or not c.alive():
                    out.append(f"{name}: {c.name} {c.crash or 'closed'}")
                    return False
        out.append(f"== {name}")
        for c in copies:
            lines = c.since(marks[c.name])
            trace = [l for l in lines if "heal trace:" in l]
            def count(text):
                return len([l for l in trace if text in l])
            heals = [l[13:150] for l in lines if ("heal: " in l or "healed our Sora" in l or "reached our Sora" in l)
                     and "heal trace" not in l]
            ups = [l[13:120] for l in lines if "downed: getting up" in l or "reached our Sora: getting up" in l]
            out.append(f"{c.name}: HP every 2 s {hp[c.name]}")
            out.append(f"{c.name}: trace lines: needs help our Sora {count('needs help our Sora')}, the Sora copy "
                       f"{count('needs help the Sora copy')}, itself {count('needs help itself')}; items used "
                       f"{count(' uses item ')}; mirrored brain lines {count('mirrored')}")
            for l in [l for l in trace if " in 10 s:" in l][-3:]:
                out.append(f"   {l[13:260]}")
            out.append(f"{c.name}: heal lines {len(heals)}: {heals[:6]}")
            def n(text):
                return len([l for l in lines if text in l])
            got_cure, got_item = n("the other player's Cure healed our Sora"), n("the other player's item healed our Sora")
            out.append(f"{c.name}: companion heal (TODO 1.58): looks after the copy {n('looks after the other player')}, "
                       f"Cure picked {n('picks Cure for the Sora copy')}, HP item picked "
                       f"{n('picks an HP item for the Sora copy')}, Cure on the copy sent "
                       f"{n('a Cure on the Sora copy heals the other player')}, item on the copy sent "
                       f"{n('on the Sora copy by a companion')}; healed by the other player's Cure {got_cure} / "
                       f"item {got_item}; the other player's heal got us up {n('reached our Sora: getting up')}")
            out.append(f"{c.name}: get-ups {ups[:2]}")
    for c in copies:
        with open(os.path.join(OUT, f"heal_{c.name}.txt"), "w", encoding="utf-8") as fh:
            fh.write("\n".join(l for l in c.since(start[c.name]) if any(k in l for k in keys)) + "\n")
    return all(c.alive() and not c.crash for c in copies)


def menu(copies, a, out):
    load_both(copies, a.slot, a.room)
    host = copies[0]
    page2 = ["tap", "down", "wait", "300"] * 3 + ["tap", "left", "wait", "1000"]  # Drive -> the Summon/Limit page
    shots = [("page2", page2), ("limit_open", ["tap", "cross", "wait", "1500"]), ("limit_back", ["tap", "circle", "wait", "800"]),
             ("summon", ["tap", "up", "wait", "500"]), ("summon_open", ["tap", "cross", "wait", "1500"])]
    for name, keys in shots:
        if keys:
            host.game.pad.run(keys)
        time.sleep(1)
        bench.screenshot(host.hwnd, os.path.join(OUT, f"menu_{name}.png"))
        out.append(f"screenshot menu_{name}.png")
    return True


SCENARIOS = {"ship": ship, "alone": alone, "downed": downed, "forms": forms, "limit": limit, "summon": summon, "party": party, "xemnas": xemnas, "xend": xend, "xlimit": xlimit, "xcontinue": xcontinue, "menu": menu, "hud": hud, "hud2": hud2, "item": item, "costume": costume, "inspect": inspect, "heal": heal}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scenario", choices=sorted(SCENARIOS))
    ap.add_argument("--slot", type=int, default=52)  # HB Borough 04/06, Donald + Goofy
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--by", choices=("A", "B"), default="A")  # limit/summon/party: who opens the menu
    ap.add_argument("--pick", type=int, default=0)  # forms/summon: which one in the list (0 = the first)
    ap.add_argument("--ship", action="store_true")  # xemnas/xlimit: through the ship-fight room first
    ap.add_argument("--hp", action="store_true")  # xemnas: keep the party HP full by writing it (for GOD=0)
    ap.add_argument("--motions", action="store_true")  # xemnas: log every motion change of each Xemnas
    ap.add_argument("--shots", action="store_true")  # xemnas: both games every 3 s after the hit; limit: both games
    ap.add_argument("--boss-hp", type=int, default=200)  # xend: every boss's HP at the start
    ap.add_argument("--seconds", type=int, default=420)  # xend: give up after this long
    ap.add_argument("--fx-census", action="store_true")  # xemnas: live effects and their particles every 1 s
    ap.add_argument("--low", type=int, default=5)  # heal: the HP a Sora is held at (a quarter of max or less)
    a = ap.parse_args()
    saves = {}
    with open(os.path.join(bench.ROOT, "build", "bench", "coop_saves.txt")) as fh:
        for line in fh:
            s, w, r = line.split()
            saves[int(s)] = (int(w, 16), int(r, 16))
    if a.scenario in ("limit", "heal") and a.slot == 52:
        a.slot = 20  # a fight (see limit)
    a.room = saves[a.slot]
    os.makedirs(OUT, exist_ok=True)
    env = ["KH2COOP_EXTRA_COPY=1"] + (["KH2COOP_GOD=0"] if a.scenario in ("downed", "hud2", "item", "costume", "xcontinue") else []) + (
        ["KH2COOP_HEAL_TRACE=1", "KH2COOP_ENEMY_GOD=1"] if a.scenario == "heal" else []) + a.env
    started = time.time()
    out = [f"{a.scenario} {time.strftime('%Y-%m-%d %H:%M:%S')} slot {a.slot} by {a.by} pick {a.pick} env {' '.join(env)}"]
    copies = bench.launch(env)
    out.append(f"pids: A {copies[0].proc.pid}, B {copies[1].proc.pid}")
    try:
        ok = SCENARIOS[a.scenario](copies, a, out)
    except (bench.Failed, SystemExit) as e:
        ok = False
        out.append(f"ERROR: {e}")
        for c in copies:
            if c.crash:
                out.append(f"{c.name} crash: {c.crash}")
            if c.alive():
                bench.screenshot(c.hwnd, os.path.join(OUT, f"{a.scenario}_{c.name}_error.png"))
    finally:
        if not a.keep:
            bench.close(copies)
    out.append(f"{'PASS' if ok else 'FAIL'} ({time.time() - started:.0f} s)")
    with open(os.path.join(OUT, f"{a.scenario}.txt"), "a", encoding="utf-8") as fh:
        fh.write("\n".join(out) + "\n\n")
    print("\n".join(out))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
