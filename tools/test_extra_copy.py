"""Automated checks of the extra copy (the Sora copy beside the party) on a bench pair, one scenario at a time.

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
  companions  TODO 1.98 (companion_sync.cpp): --by A/B uses a Drive Form (--what form --pick N) or a summon
           (--what summon) in HB Borough (no enemies), then Reverts / dismisses: the same companions leave and come
           back in both games (sampled every 0.25 s).
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
  mickey   TODO 1.3 (god mode off): Mickey forced in the host's game only (in memory: MickeyAllowed returns 1, the
           appearance count 0), slot 52 (no enemies). The friend down, then the host: Game Over; the host picks 'I
           won't give up', the bench presses triangle (Healing Light) until the host's Sora gets up, then walks and
           attacks. Each game's lines in mickey_<copy>.txt, screenshots mickey_<copy>_<step>.png.
  mickey_coop  TODO 1.3 / Q6 (mickey_sync.cpp): Mickey allowed in both games (memory), the roll's winner set (--winner
           A/B); both down; only the winner chooses; the loser comes back down; Healing Light gets both up. --trace.
           --end over / dies / nobody: 'It's all over', Mickey's death, no winner: both at Continue / Load, host Continues.
  getup    god mode off, save 52: the friend down (KO face, no low-HP beep), up on its own with 26% of max HP.
  mickey_real  save 20, the Twin Lords fight: Mickey (the host's, both down) left at 2 HP, then the bosses' real hits;
           what the game does at his 0 HP is logged for 120 s (mreal_<copy>_<s>.png, mreal_<copy>.txt).
  heal     TODO 1.52 / 1.58 (save 20, the Twin Lords fight, god mode for the party and the enemies, the companions'
           heal choice logged with KH2COOP_HEAL_TRACE=1): (1) the friend's Sora held at --low HP (debug lowhp): who
           sees it as needing help, who heals it; (2) the friend down (a 9999 hit): does a companion get it up;
           (3, 4) the same for the host's Sora (the control: the game's own heals). Screenshots heal_<phase>_<copy>_<n>,
           every trace and heal line in heal_<copy>.txt. PASS = no crash (the findings are in the summary).
  swarm    your setup 2026-10-18 (save 10, warp to the Land of Dragons climb 08/07, Shan-Yu's swarm): god mode,
           one-hit kills (KH2COOP_TEST_ONE_HIT=1), both lock on, attack and jump until the mission ends; each
           game's actor budget and enemy count every 2 s (swarm_timeline.txt), screenshots swarm_<copy>_<s>.png.
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
    # The friend's Sora: a lethal hit on its copy in the host's game, forwarded (the friend's game drops other damage on
    # its own Sora). Its own hit rules may keep it at 1 HP (Second Chance, Once More: TODO 1.77 / 1.91); then another.
    for n in range(3):
        bench.debug("A", 3, 9999)
        time.sleep(1)
        fl = friend.since(marks["B"])
        down = find(fl, "down instead of dead")
        saved = [l[13:] for l in find(fl, "the peer's hit (-9999) on our Sora:")]
        if down or len(saved) <= n:
            break
        out.append(f"friend lethal hit {n + 1}: kept at 1 HP by its own rules ({saved[n][:90]})")
        time.sleep(3)  # out of the hit reaction, so Once More doesn't apply to the next one
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


def special(copies, a, out, name, keys, picks=None, warp=None, steps=4, during=None):
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
        if step == 3 and during:
            during()
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


# Fights to use a Limit in, by save: 20 = Agrabah's Twin Lords (Donald + Aladdin), 97 = Hollow Bastion's Borough
# Heartless (Donald + Goofy: Trinity; test_census.py 'borough'). (world, room, program, door)
LIMIT_FIGHTS = {20: (0x07, 0x03, 59), 97: (0x04, 0x09, 1, 2)}


def limit(copies, a, out):
    marks = {c.name: c.mark() for c in copies}
    # A Limit only starts in a fight: on save 52's Borough save point and on the street next to it the Limit list
    # never opened (session 32, 'host commands: []' in every earlier run). Save 20 -> the Twin Lords fight.
    # 6 presses: a hit on the presser's Sora can swallow one (the Lords go for the host's).
    user, other = (copies[1], copies[0]) if a.by == "B" else (copies[0], copies[1])
    tried = {}

    def other_tries():  # --other-tries: the other player picks a Limit 2 s into this one (DECISIONS L2)
        import threading

        def run():
            end = time.time() + 180  # the save load and warp come first
            while time.time() < end and not find(user.since(marks0[user.name]), "our Limit started"):
                time.sleep(0.2)
            time.sleep(2)
            tried["mark"] = other.mark()
            tried["user_started"] = bool(find(user.since(marks0[user.name]), "our Limit started"))
            try_limit()

        marks0 = {c.name: c.mark() for c in copies}
        threading.Thread(target=run, daemon=True).start()

    def try_limit():
        other.game.pad.run(PAGE2)
        bench.screenshot(other.hwnd, os.path.join(OUT, f"limit_{other.name}_try.png"))
        other.game.pad.run(["tap", "cross", "wait", "600"])
        bench.screenshot(other.hwnd, os.path.join(OUT, f"limit_{other.name}_try2.png"))
        other.game.pad.run(["tap", "cross", "wait", "600"])
        bench.screenshot(other.hwnd, os.path.join(OUT, f"limit_{other.name}_try3.png"))

    if a.other_tries:
        other_tries()
    ok = special(copies, a, out, "limit", [], ["tap", "down", "wait", "300"] * a.pick,
                 warp=LIMIT_FIGHTS.get(a.slot), steps=6,
                 during=None)
    if "mark" in tried:
        ol = other.since(tried["mark"])
        second = find(ol, "type 0x51)")
        refused = find(ol, "refused: the other player's Limit runs")
        out.append(f"{other.name}: tried a Limit during the other's (its start seen {tried['user_started']}): Limit "
                   f"commands {len(second)} (0 expected), refused {len(refused)}, screenshots "
                   f"limit_{other.name}_try*.png")
        ok = ok and tried["user_started"] and not second
    started = find(user.since(marks[user.name]), "type 0x51)")  # a Limit command
    out.append(f"{user.name}: Limit commands {len(started)}")
    for c in copies:  # limit_sync.cpp: the other game's allies leave and come back with the Limit (DECISIONS L1)
        sync = [l[13:150] for l in c.since(marks[c.name]) if "limit sync:" in l]
        out.append(f"{c.name}: limit sync lines {len(sync)}: {sync[:6]}")
    ok = ok and bool(started)
    if a.by == "B":  # TODO 1.57: the host's companion plays the friend's Limit partner
        lines = other.since(marks[other.name])
        borrowed = find(lines, "plays the friend's Limit partner")
        back = find(lines, "is back with our Sora")
        out.append(f"{other.name}: partner borrowed {len(borrowed)}, back {len(back)} ({(back or ['-'])[0][13:140]})")
        has_partner = find(user.since(marks[user.name]), "plays its partner's part")  # a Limit with no partner: none
        ok = ok and (bool(borrowed) or not has_partner)
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


def party_now(c):
    """Which of Donald (P_EX020) and Goofy (P_EX030) are in this game's actor list."""
    return tuple(bool(bench.actor_by_model(c, m)) for m in ("P_EX020", "P_EX030"))


def companions(copies, a, out):
    """TODO 1.98 / DECISIONS C1 (companion_sync.cpp): --by A/B uses a Drive Form (--what form, --pick: 0 Valor, 1
    Wisdom, ... as listed) or a summon (--what summon, --pick in its list) in a room without enemies (save 52, HB
    Borough); then Reverts / dismisses. Who of Donald and Goofy is in each game is sampled every 0.25 s, before,
    during and after. PASS = during it the same companions are gone in both games (at least one), after it both are
    back in both games, no crash. Timelines and screenshots companions_<copy>_<phase>.png."""
    load_both(copies, a.slot, a.room)
    p = copies[1] if a.by == "B" else copies[0]
    marks = {c.name: c.mark() for c in copies}
    out.append(f"before: (Donald, Goofy) present " + ", ".join(f"{c.name} {party_now(c)}" for c in copies))
    timeline = {c.name: [] for c in copies}
    start = time.time()

    def sample(secs):
        end = time.time() + secs
        while time.time() < end:
            for c in copies:
                now = party_now(c)
                if not timeline[c.name] or timeline[c.name][-1][1] != now:
                    timeline[c.name].append((round(time.time() - start, 2), now))
                c.lines()
                if c.crash or not c.alive():
                    raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
            time.sleep(0.25)

    o = copies[0] if p is copies[1] else copies[1]
    if a.what == "form":
        p.game.pad.run(DRIVE + ["tap", "cross", "wait", "800"] + ["tap", "down", "wait", "300"] * a.pick +
                       ["tap", "cross", "wait", "500"])
        sample(10)
        if a.overlap:  # DECISIONS C2: the other player's Drive list without the form whose companion is away
            om = o.mark()
            o.game.pad.run(DRIVE + ["tap", "cross", "wait", "1200"])
            bench.screenshot(o.hwnd, os.path.join(OUT, f"companions_{o.name}_drive_list.png"))
            o.game.pad.run(["tap", "cross", "wait", "500"])  # the first form offered
            sample(10)
            out.append(f"overlap: {o.name}'s form: {[l[13:150] for l in o.since(om) if 'our Drive Form sends' in l]}")
            p.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])  # first one reverts
            sample(15)
            out.append(f"overlap: after {p.name}'s Revert (Donald, Goofy): " +
                       ", ".join(f"{c.name} {party_now(c)}" for c in copies))
            p = o  # the other one reverts below
    else:
        p.game.pad.run(PAGE2 + ["tap", "up", "wait", "500", "tap", "up", "wait", "500"])
        for step in range(4):
            sample(2.5)
            if step == 1:
                p.game.pad.run(["tap", "down", "wait", "300"] * a.pick)
            p.game.pad.run(["tap", "cross", "wait", "300"])
        sample(10)
    during = {c.name: party_now(c) for c in copies}
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"companions_{c.name}_during.png"))
    if a.what == "form":
        p.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])  # Revert
    else:
        p.game.pad.run(["tap", "down", "wait", "500", "tap", "cross", "wait", "500"])  # Dismiss
    sample(15)
    after = {c.name: party_now(c) for c in copies}
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"companions_{c.name}_after.png"))
    for c in copies:
        out.append(f"{c.name} timeline (s, (Donald, Goofy)): {timeline[c.name]}")
        lines = [l[13:170] for l in c.since(marks[c.name]) if "companion sync:" in l]
        out.append(f"{c.name} companion sync lines ({len(lines)}):")
        out.extend(f"    {l}" for l in lines[:25])
    out.append(f"during: {during}; after: {after}")
    same = during["A"] == during["B"] and not all(during["A"])
    back = all(all(v) for v in after.values())
    return same and back and all(c.alive() and not c.crash for c in copies)


def companion_now(c, model):
    """(HP, max HP, MP, KO'd, motion) of this game's companion with that model, or None."""
    a = bench.actor_by_model(c, model)
    if not a:
        return None
    try:
        st = read_u(c, a + 0x5C0, 8)
        hp, mx = struct.unpack("<ii", peek.read(c.game.h, st, 8))
        mp = struct.unpack("<i", peek.read(c.game.h, st + 0x180, 4))[0]
        return hp, mx, mp, bool(read_u(c, a + 0x9B8) & 4), read_u(c, a + 0x180)
    except (OSError, struct.error):
        return None


def companion_ko(copies, a, out):
    """TODO 1.98 part 2 / 1.43 (companion_sync.cpp status): in a room without enemies (save 52) the host's Goofy is
    KO'd (a 9999 debug hit), the friend's copy must go down too (its KO bit) and get up when the host's does; the host's
    Donald's MP (written) must show on the friend's Donald; the friend's Cure on a hurt Goofy must heal the host's Goofy
    (each spell slot cast in turn; only Cure heals). The friend's mirrored companions must not think (heal trace).
    PASS = KO and get-up in both within 1 s of each other, MP equal, the host's Goofy healed by the friend, no crash."""
    load_both(copies, a.slot, a.room)
    host, friend = copies
    marks = {c.name: c.mark() for c in copies}
    out.append("before (HP, max, MP, KO, motion): " + ", ".join(
        f"{c.name} Donald {companion_now(c, 'P_EX020')} Goofy {companion_now(c, 'P_EX030')}" for c in copies))
    slot = 1 + [read_u(host, host.game.base + 0x2A239B0 + 8 * i, 8) for i in range(2)].index(
        bench.actor_by_model(host, "P_EX030"))
    timeline = {c.name: [] for c in copies}
    start = time.time()

    def sample(secs, model="P_EX030"):
        end = time.time() + secs
        while time.time() < end:
            for c in copies:
                now = companion_now(c, model)
                key = now and (now[3], now[0] > 0, now[4])
                if not timeline[c.name] or timeline[c.name][-1][1] != key:
                    timeline[c.name].append((round(time.time() - start, 2), key, now and now[0]))
                c.lines()
                if c.crash or not c.alive():
                    raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
            time.sleep(0.1)

    sample(1)
    bench.debug("A", slot, 9999)  # the host's Goofy: KO
    sample(20)
    for c in copies:
        out.append(f"{c.name} Goofy timeline (s, (KO, HP > 0, motion), HP): {timeline[c.name]}")

    def first(name, want):
        return next((t for t, k, _ in timeline[name] if k and k[0] == want), None)
    down = {c.name: first(c.name, True) for c in copies}
    up = {c.name: next((t for t, k, _ in timeline[c.name] if k and not k[0] and down[c.name] and t > down[c.name]), None)
          for c in copies}
    out.append(f"KO at {down}, up at {up}")
    full = [(t, hp) for t, k, hp in timeline["B"] if k and k[0] and hp]
    out.append(f"friend's KO'd Goofy with HP above 0 (s, HP): {full} (none = its HUD shows the KO)")
    kos = len(find(friend.since(marks["B"]), "is KO'd: ours too"))
    out.append(f"KO lines on the friend: {kos} (1 = its copy stayed down)")
    ko_ok = all(down.values()) and all(up.values()) and abs(down["A"] - down["B"]) < 1 and abs(up["A"] - up["B"]) < 1         and kos == 1 and not full

    donald = bench.actor_by_model(host, "P_EX020")
    st = read_u(host, donald + 0x5C0, 8)
    mx = struct.unpack("<i", peek.read(host.game.h, st + 0x184, 4))[0]
    write_mem(host, st + 0x180, struct.pack("<i", mx // 3))
    time.sleep(1.5)
    mp = {c.name: companion_now(c, "P_EX020") for c in copies}
    out.append(f"host's Donald MP written {mx // 3}/{mx}: Donald now {mp}")
    mp_ok = all(v and abs(v[2] - mx // 3) <= 2 for v in mp.values())  # Donald recharges out of a fight
    write_mem(host, st + 0x180, struct.pack("<i", mx))

    bench.debug("A", slot, 10)  # the host's Goofy hurt
    time.sleep(1.5)
    hurt = companion_now(host, "P_EX030")
    out.append(f"Goofy after a 10 hit: host {hurt}, friend {companion_now(friend, 'P_EX030')}")
    data = [read_u(friend, friend.game.base + 0x2A23A80 + 0x50 * i + 0x48, 8) for i in range(6)]
    spells = [(i, read_u(friend, d + 0x26, 2)) for i, d in enumerate(data) if d]
    out.append(f"friend's spell slots (slot, id): {[(i, hex(x)) for i, x in spells]}")
    healed = None
    for i, _ in spells:
        fm = friend.mark()
        bench.debug("B", 14, i)  # the friend's Sora casts that slot
        time.sleep(4)
        if find(friend.since(fm), "sent to the host's game"):
            healed = i
            break
    after = companion_now(host, "P_EX030")
    out.append(f"friend's cast that healed Goofy: slot {healed}; host's Goofy {hurt} -> {after}, friend's "
               f"{companion_now(friend, 'P_EX030')}")
    heal_ok = healed is not None and hurt and after and after[0] > hurt[0]
    revive_ok = True
    if healed is not None:  # the friend's Cure on a KO'd Goofy: the host's gets up before its timer (~4.2 s)
        bench.debug("A", slot, 9999)
        time.sleep(1)
        ko = {c.name: companion_now(c, "P_EX030") for c in copies}
        t0 = time.time()
        bench.debug("B", 14, healed)
        up_at = None
        while time.time() - t0 < 3 and up_at is None:
            now = companion_now(host, "P_EX030")
            if now and not now[3]:
                up_at = round(time.time() - t0, 2)
            time.sleep(0.05)
        time.sleep(0.5)
        out.append(f"KO'd Goofy {ko}, the friend's Cure: host's up after {up_at} s; now host "
                   f"{companion_now(host, 'P_EX030')}, friend {companion_now(friend, 'P_EX030')}")
        revive_ok = up_at is not None and all(v and v[3] for v in ko.values())
        friend_now = companion_now(friend, "P_EX030")
        revive_ok = revive_ok and friend_now and not friend_now[3]
    for c in copies:
        lines = [l[13:170] for l in c.since(marks[c.name]) if "companion sync:" in l and "places" not in l]
        out.append(f"{c.name} companion sync lines ({len(lines)}):")
        out.extend(f"    {l}" for l in lines[:20])
        trace = [l[13:200] for l in c.since(marks[c.name]) if "heal trace:" in l and "in 10 s" in l]
        out.append(f"{c.name} think counts: {trace[-2:]}")
    skipped = any("skipped" in l and "0 thinks (" in l for l in friend.since(marks["B"]) if "in 10 s" in l)
    out.append(f"KO {ko_ok}, MP {mp_ok}, friend's heal {heal_ok}, friend's Cure revives {revive_ok}, friend's "
               f"companions not thinking {skipped}")
    return ko_ok and mp_ok and heal_ok and revive_ok and all(c.alive() and not c.crash for c in copies)


def stat_dump(c, actor, n=0x1D0):
    """The actor's stat block (actor+0x5C0) as i32s, or None."""
    try:
        st = struct.unpack("<Q", peek.read(c.game.h, actor + 0x5C0, 8))[0]
        return st, struct.unpack(f"<{n // 4}i", peek.read(c.game.h, st, n))
    except (OSError, struct.error):
        return None


def mp_probe(copies, a, out):
    """Research (part 2 of companion sync): where MP is in the stat block. The host's Sora casts the first spell
    of its Magic menu; Sora's and Donald's stat blocks are read before and after, and the changed fields listed."""
    load_both(copies, a.slot, a.room)
    host = copies[0]
    sora = head(host)
    donald = bench.actor_by_model(host, "P_EX020")
    before = {"Sora": stat_dump(host, sora), "Donald": stat_dump(host, donald)}
    for who, d in before.items():
        if d:
            out.append(f"{who} stat block exe+0x{d[0] - host.game.base:X}: first 8 {d[1][:8]}, +0x180.. {d[1][0x60:0x68]}")
    host.game.pad.run(["tap", "down", "wait", "400", "tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])
    time.sleep(4)
    bench.screenshot(host.hwnd, os.path.join(OUT, "mp_probe_after.png"))
    for who in ("Sora", "Donald"):
        actor = sora if who == "Sora" else donald
        b, now = before[who], stat_dump(host, actor)
        if b and now:
            diffs = [(f"+0x{4 * i:X}", x, y) for i, (x, y) in enumerate(zip(b[1], now[1])) if x != y]
            out.append(f"{who} changed after the spell: {diffs}")
    return all(c.alive() and not c.crash for c in copies)


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
        if grabbed(host) and not grabbed(copies[1]):  # the host's Sora held: the friend's game has no grab scene
            end = min(end, time.time() + 10)
        if a.shots and shot < 12:  # what Xemnas does in the next ~36 s
            for c in copies:
                if c.alive():
                    bench.screenshot(c.hwnd, os.path.join(OUT, f"xemnas_{c.name}_t{shot:02d}.png"))
            shot += 1
            time.sleep(2)
        time.sleep(1)
    on_host = grabbed(host) and not grabbed(copies[1])  # Xemnas went for the host's Sora (aggro.cpp, TODO 1.62)
    if on_host:
        freed = riku_part(copies, out)
    elif any(grabbed(c) for c in copies):
        time.sleep(20)
    for c in copies:
        if c.alive():
            bench.screenshot(c.hwnd, os.path.join(OUT, f"xemnas_{c.name}.png"))
        out.append(f"{c.name}: grab {grabbed(c)}, alive {c.alive()}, crash {c.crash or '-'}")
    if on_host:
        return freed and all(c.alive() and not c.crash for c in copies)
    return all(grabbed(c) and c.alive() and not c.crash for c in copies)


def actor_pos(c, a):
    return struct.unpack("<3f", peek.read(c.game.h, a + 0x640 + 0x30, 12))


def riku_part(copies, out):
    """The host's Sora held by Xemnas: the host plays Riku (Xemnas's script moves him from the stick) up to the
    Xemnas holding the stand-in Sora (P_EX100_LAST) and presses triangle until Sora is free. Shots of both games."""
    import math
    host = copies[0]
    end = time.time() + 30
    while time.time() < end and not bench.actor_by_model(host, "P_EX100_LAST"):
        time.sleep(0.5)
    if not bench.actor_by_model(host, "P_EX100_LAST"):
        out.append("Riku part: no P_EX100_LAST in the host's game within 30 s")
        return False
    out.append("Riku part: on (the stand-in Sora P_EX100_LAST is in the host's game)")
    for c in copies[1:]:
        out.append(f"{c.name}: P_EX100_LAST {'yes' if bench.actor_by_model(c, 'P_EX100_LAST') else 'no'}")
    start, end, shot, near = time.time(), time.time() + 120, 0, 0
    last_shot = 0.0
    while time.time() < end:
        if any(c.crash or not c.alive() for c in copies):
            break
        try:
            stand_in = bench.actor_by_model(host, "P_EX100_LAST")
            if not stand_in:
                break
            riku = bench.actor_by_model(host, "P_EH000_LAST")
            sx, _, sz = actor_pos(host, stand_in)
            xem = [a for a, n in bench.actors(host) if n.startswith("B_EX170_LAST")]
            holder = min(xem, key=lambda a: math.hypot(actor_pos(host, a)[0] - sx, actor_pos(host, a)[2] - sz))
            tx, _, tz = actor_pos(host, holder)
            rx, _, rz = actor_pos(host, riku)
        except (OSError, ValueError):
            time.sleep(0.3)
            continue
        if time.time() - last_shot > 6:
            last_shot = time.time()
            for c in copies:
                if c.alive():
                    bench.screenshot(c.hwnd, os.path.join(OUT, f"xemnas_riku_{c.name}_{shot:02d}.png"))
            shot += 1
        d = math.hypot(tx - rx, tz - rz)
        if d < 250:
            near += 1
            host.game.pad.run(["tap", "triangle", "wait", "300"])
            continue
        cx, _, cz = host.game.camera()  # stick relative to the camera, as nav.push but from Riku
        fx, fz = rx - cx, rz - cz
        n = math.hypot(fx, fz) or 1.0
        fx, fz = fx / n, fz / n
        dx, dz = (tx - rx) / d, (tz - rz) / d
        host.game.pad.send(0, lx=dx * fz - dz * fx, ly=dx * fx + dz * fz, ms=150)
        time.sleep(0.15)
    host.game.pad.send()
    freed = not bench.actor_by_model(host, "P_EX100_LAST") if host.alive() else False
    out.append(f"Riku part: {'Sora freed' if freed else 'still held'} after {time.time() - start:.0f} s, "
               f"triangle presses near Xemnas {near}, shots {shot}")
    if freed:
        time.sleep(15)
        for c in copies:
            if c.alive():
                bench.screenshot(c.hwnd, os.path.join(OUT, f"xemnas_freed_{c.name}.png"))
    return freed


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
            c = copies[turn % 2] if not a.host_only else copies[0]
            turn += 1
            if rooms[c.name] == FINAL[:2]:
                c.game.pad.run(["tap", "cross", "wait", "200", "tap", "triangle", "wait", "200"])
            if time.time() > sample:
                sample = time.time() + 2
                row = [f"{time.time() - t0:6.1f}"]
                for c in copies:
                    try:
                        listed = bench.actors(c)
                        bosses[c.name] = [(x, n) for x, n in listed if n.startswith("B_EX170")]
                        if c is copies[0] and any(n == "P_EX100_LAST" for _, n in listed):
                            riku_part(copies, out)  # the host's Sora held: walk Riku to Xemnas, Rescue
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


def write_mem(c, addr, data):
    """Writes bytes into a copy's memory (code too: the page is made writable for it)."""
    import ctypes
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.OpenProcess.restype = ctypes.c_void_p
    h = k32.OpenProcess(0x0008 | 0x0010 | 0x0020, False, c.game.pid)  # VM_OPERATION | READ | WRITE
    old, done = ctypes.c_uint32(), ctypes.c_size_t()
    k32.VirtualProtectEx(ctypes.c_void_p(h), ctypes.c_void_p(addr), len(data), 0x40, ctypes.byref(old))
    ok = k32.WriteProcessMemory(ctypes.c_void_p(h), ctypes.c_void_p(addr), data, len(data), ctypes.byref(done))
    k32.VirtualProtectEx(ctypes.c_void_p(h), ctypes.c_void_p(addr), len(data), old.value, ctypes.byref(old))
    k32.CloseHandle(ctypes.c_void_p(h))
    if not ok or done.value != len(data):
        raise bench.Failed(f"write to {c.name} exe+0x{addr - c.game.base:X} failed")


MICKEY_ALLOWED, MICKEY_COUNT, GAME_OVER = 0x3A3BD0, 0x9AD660, 0x2AE8050  # VERIFIED_OFFSETS 'Mickey rescue'


MICKEY_SPAWN = 0x4158C0  # VERIFIED_OFFSETS 'Mickey rescue' (Mickey spawn), Game Over state 7
LOW_HP_BEEP = 0xAD8040  # u8: the low-HP beep is on (VERIFIED_OFFSETS lead 'The player's own gauge ... low-HP beep')


def actor_hp(c, a):
    """An actor's current HP (its stats at +0x5C0), or None."""
    try:
        stats = read_u(c, a + 0x5C0, 8)
        return struct.unpack("<i", peek.read(c.game.h, stats, 4))[0] if stats else None
    except OSError:
        return None


def player(c):
    """The actor the game controls (exe+0x2A105D0): our Sora, or Mickey during his rescue (not the list head then)."""
    return read_u(c, c.game.base + 0x2A105D0, 8)


def head(c):
    """The actor list head: our Sora, Mickey during his rescue (puppet.cpp NotePlayer reads the same)."""
    return read_u(c, c.game.base + 0x2A171C8, 8)


def mickey_trace(copies, stop):
    """--trace: every 50 ms, the fades and Game Over flags in each game; a line per change (mickey_trace_<copy>.txt)."""
    import threading
    fields = [("fadeA.mode", 0xABB290 + 0x90, 8), ("fadeA.white", 0xABB290 + 0x100, 1), ("fadeA.state", 0xABB290 + 0x108, 4),
              ("fadeA.len", 0xABB290 + 0x10C, 4), ("fadeB.mode", 0xABB3B0 + 0x90, 8), ("fadeB.white", 0xABB3B0 + 0x100, 1),
              ("fadeB.state", 0xABB3B0 + 0x108, 4), ("fadeB.len", 0xABB3B0 + 0x10C, 4), ("f500", 0x2A10500, 4),
              ("f504", 0x2A10504, 4), ("f508", 0x2A10508, 4), ("f11400", 0x2A11400, 4), ("pause18", 0xABAD18, 4),
              ("abb878", 0xABB878, 4), ("ad93c0", 0xAD93C0, 1), ("menu", 0x7435D0, 4), ("player", 0x2A105D0, 8),
              ("gameover", GAME_OVER, 8)]

    def run(c):
        last, lines, t0 = {}, [], time.time()
        while not stop:
            try:
                vals = {n: read_u(c, c.game.base + a, z) for n, a, z in fields}
                g = vals["gameover"]
                vals["go.state"] = read_u(c, g + 0x10) if g else -1
            except OSError:
                break
            ch = [f"{n}={v:#x}" for n, v in vals.items() if last.get(n) != v]
            if ch:
                lines.append(f"{time.strftime('%H:%M:%S')}.{int(time.time() * 1000) % 1000:03d} +{time.time() - t0:6.2f} "
                             + " ".join(ch))
                last = vals
            time.sleep(0.05)
        with open(os.path.join(OUT, f"mickey_trace_{c.name}.txt"), "w", encoding="utf-8") as fh:
            fh.write("\n".join(lines) + "\n")
    ts = [threading.Thread(target=run, args=(c,), daemon=True) for c in copies]
    for t in ts:
        t.start()
    return ts


def mickey(copies, a, out):
    """TODO 1.3 (god mode off): Mickey forced in the host's game only (MickeyAllowed exe+0x3A3BD0 -> mov al,1; ret and
    the appearance count 0 = a 100% roll, as in the 2026-10-06 single-copy test), in a room with no enemies. The friend
    goes down, then the host: both down = Game Over; the host picks 'I won't give up', Mickey comes, the bench presses
    triangle (Healing Light) until the host's Sora gets up. Logs each step in both games; PASS = the host's Sora is back
    with HP and control, no crash in either game (the friend's side is recorded, not judged: it stays at its Game Over)."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    # off the save point the save loads us on (it refills HP: the first run's friend was back at 60 before each hit)
    bench.both(lambda c: c.game.pad.run(["stick", "0", "1", "2500", "wait", "1500"]), copies)
    for c in copies:
        out.append(f"{c.name}: room {c.room()}, enemies {len(c.game.enemies())}, Sora HP {sora_hp(c)}, "
                   f"{c.game.where()}")
    count = read_u(host, host.game.base + MICKEY_COUNT, 2)
    write_mem(host, host.game.base + MICKEY_ALLOWED, b"\xB0\x01\xC3")
    write_mem(host, host.game.base + MICKEY_COUNT, b"\x00\x00")
    out.append(f"host: Mickey allowed forced, appearance count {count} -> 0 (in memory only)")
    if a.friend_back:  # Q6 probe: the friend's game takes the Mickey branch too (it keeps the field), with no Mickey
        fcount = read_u(friend, friend.game.base + MICKEY_COUNT, 2)
        write_mem(friend, friend.game.base + MICKEY_ALLOWED, b"\xB0\x01\xC3")
        write_mem(friend, friend.game.base + MICKEY_COUNT, b"\x00\x00")
        write_mem(friend, friend.game.base + MICKEY_SPAWN, b"\xC3")
        out.append(f"friend: Mickey allowed forced, appearance count {fcount} -> 0, Mickey's spawn exe+0x{MICKEY_SPAWN:X} "
                   f"-> ret (in memory only)")
    sora, fsora = head(host), head(friend)  # each game's own Sora, before Mickey
    stop = []
    tracers = mickey_trace(copies, stop) if a.trace else []
    marks = {c.name: c.mark() for c in copies}
    for n in range(3):  # the friend down (as in 'downed': its own rules may keep it at 1 HP once)
        bench.debug("A", 3, 9999)
        time.sleep(1)
        if find(friend.since(marks["B"]), "down instead of dead"):
            break
        time.sleep(3)
    out.append(f"friend down: {bool(find(friend.since(marks['B']), 'down instead of dead'))}")
    bench.debug("A", 0, 9999)  # the host too: both down
    go = lambda c: read_u(c, c.game.base + GAME_OVER, 8)
    bench.wait("the host's Game Over", lambda: go(host), 40, copies)
    time.sleep(3)
    for c in copies:
        g = go(c)
        out.append(f"{c.name}: Game Over object {'yes' if g else 'no'}, Mickey chosen "
                   f"{read_u(c, g + 0x14, 1) if g else '-'}, state {read_u(c, g + 0x10) if g else '-'}")
        bench.screenshot(c.hwnd, os.path.join(OUT, f"mickey_{c.name}_1_gameover.png"))
    if not go(host) or read_u(host, go(host) + 0x14, 1) != 1:
        out.append("host: no Mickey on the Game Over (the roll or a companion check said no)")
        return False
    time.sleep(4)  # the white screen fades in
    host.game.pad.run(["tap", "cross", "wait", "300"])  # 'I won't give up' (the first choice)
    bench.wait("Mickey is the player", lambda: head(host) not in (0, sora), 40, copies)
    time.sleep(3)
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"mickey_{c.name}_2_mickey.png"))
    out.append(f"host: Mickey is the player; host's Sora motion {bench.motion(host, sora)} (252 = lying)")
    if a.friend_back:
        fstate = lambda: read_u(friend, go(friend) + 0x10) if go(friend) else -1
        bench.wait("the friend's white screen", lambda: fstate() == 6 or not go(friend), 30, copies)
        out.append(f"friend: Game Over state {fstate()}, Mickey chosen {read_u(friend, go(friend) + 0x14, 1) if go(friend) else '-'}")
        time.sleep(1)
        friend.game.pad.run(["tap", "cross", "wait", "300"])  # 'I won't give up' in the friend's game, no Mickey there
        t0 = time.time()
        bench.wait("the friend's Game Over to end", lambda: not go(friend), 20, copies)
        out.append(f"friend: Game Over ended {time.time() - t0:.1f} s after its 'I won't give up'")
        fseen = []
        for _ in range(16):  # 8 s of the friend's own Sora
            m = (bench.motion(friend, fsora), actor_hp(friend, fsora))
            if not fseen or fseen[-1] != m:
                fseen.append(m)
            time.sleep(0.5)
        bench.screenshot(friend.hwnd, os.path.join(OUT, "mickey_B_2b_back.png"))
        out.append(f"friend back: player {bench.model_of(friend, head(friend))} (own Sora {head(friend) == fsora}), "
                   f"own Sora (motion, HP) {fseen}, Donald {bool(bench.actor_by_model(friend, 'P_EX020'))}, "
                   f"Goofy {bool(bench.actor_by_model(friend, 'P_EX030'))}, copy {bench.model_of(friend, head(friend))}, "
                   f"actors {[n for _, n in bench.actors(friend)][:12]}")
    mickey_actor = head(host)
    out.append(f"host: the player is now {bench.model_of(host, mickey_actor)} (exe+0x{mickey_actor - host.game.base:X}), "
               f"was exe+0x{sora - host.game.base:X}; the player pointer has {bench.model_of(host, player(host))}")
    seen, presses, start = {"sora": [], "mickey": []}, 0, time.time()
    up = lambda: bench.motion(host, sora) == 253 or head(host) == sora
    while time.time() - start < 150 and not up():  # Healing Light: triangle, ~20 presses with no enemies
        host.game.pad.run(["tap", "triangle", "wait", "250"])
        presses += 1
        for key, act in (("sora", sora), ("mickey", mickey_actor)):
            m = bench.motion(host, act)
            if not seen[key] or seen[key][-1] != m:
                seen[key].append(m)
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'} during Mickey: {c.crash or ''}")
        if presses == 15:
            bench.screenshot(host.hwnd, os.path.join(OUT, "mickey_A_3_healing.png"))
    out.append(f"host: {presses} triangle presses in {time.time() - start:.0f} s, Sora up {up()}; "
               f"motions Sora {seen['sora']}, Mickey {seen['mickey']}")
    time.sleep(10)  # the get-up (253, ~5 s), then control back
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"mickey_{c.name}_4_after.png"))
    host.game.pad.run(["stick", "0", "1", "1500", "wait", "300", "tap", "cross", "wait", "500"])  # walk, attack
    time.sleep(3)
    ok = head(host) == sora and (sora_hp(host) or 0) > 0
    keys = ("game over", "downed:", "switched actors", "build member", "not built", "extra copy:", "grab sync",
            "exception", "CRASH")
    for c in copies:
        lines = c.since(marks[c.name])
        g = go(c)
        out.append(f"{c.name}: player {bench.model_of(c, head(c))} (pointer {bench.model_of(c, player(c))}), "
                   f"Sora HP {sora_hp(c)}, Game Over object {'yes, state %d' % read_u(c, g + 0x10) if g else 'no'}, "
                   f"room {c.room()}, alive {c.alive()}, crash {c.crash or '-'}")
        with open(os.path.join(OUT, f"mickey_{c.name}.txt"), "w", encoding="utf-8") as fh:
            fh.write("\n".join(l for l in lines if any(k in l for k in keys)) + "\n")
        bench.screenshot(c.hwnd, os.path.join(OUT, f"mickey_{c.name}_5_end.png"))
        ok &= c.alive() and not c.crash
    out.append(f"host: walked and attacked; HP {sora_hp(host)}")
    if a.friend_back:
        out.append(f"friend at the end: own Sora motion {bench.motion(friend, fsora)}, HP {actor_hp(friend, fsora)}, "
                   f"Game Over {'yes' if go(friend) else 'no'}")
        ok &= not go(friend)
    stop.append(1)
    for t in tracers:
        t.join(2)
    return ok


def jump_test(c, actor):
    """Your idea 2026-10-18 (the Mickey loser floated after a knock-up): tap jump, sample the height (actor+0x670 y)
    every 0.1 s for 3 s. Returns (start, highest change, end change)."""
    y = lambda: struct.unpack("<f", peek.read(c.game.h, actor + 0x674, 4))[0]
    y0, ys = y(), []
    c.game.pad.run(["tap", "circle", "wait", "100"])
    end = time.time() + 3
    while time.time() < end:
        ys.append(y())
        time.sleep(0.1)
    far = max(ys, key=lambda v: abs(v - y0))
    return round(y0, 1), round(far - y0, 1), round(ys[-1] - y0, 1)


DUMP_ACTOR, DUMP_MOVE = 0x1000, 0xD00  # bytes of the actor and of its movement object (actor+0xDC0)


def dump_sora(c, actor):
    """--dump: the actor's first DUMP_ACTOR bytes and its movement object's (actor+0xDC0) first DUMP_MOVE bytes."""
    move = read_u(c, actor + 0xDC0, 8)
    return peek.read(c.game.h, actor, DUMP_ACTOR), peek.read(c.game.h, move, DUMP_MOVE) if move else b""


def dump_diff(before, after, ref_before, ref_after, out, label):
    """u32 fields that changed on the loser (before -> after) but not on the winner (its own before -> after)."""
    rows = []
    for part, (b, a, rb, ra) in (("actor", (before[0], after[0], ref_before[0], ref_after[0])),
                                 ("move", (before[1], after[1], ref_before[1], ref_after[1]))):
        for off in range(0, min(len(b), len(a), len(rb), len(ra)) - 3, 4):
            x, y = b[off:off + 4], a[off:off + 4]
            if x != y and rb[off:off + 4] == ra[off:off + 4]:
                rows.append(f"{part}+0x{off:X}: {x[::-1].hex()} -> {y[::-1].hex()} (winner {rb[off:off + 4][::-1].hex()})")
    with open(os.path.join(OUT, f"mcoop_dump_{label}.txt"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(rows) + "\n")
    out.append(f"dump {label}: {len(rows)} u32 fields changed only on the loser (mcoop_dump_{label}.txt)")


def mcoop_lines(copies, marks, stop, tracers):
    """Stops the --trace samplers; each game's Mickey / downed / Game Over lines in mcoop_<copy>.txt."""
    stop.append(1)
    for t in tracers:
        t.join(2)
    keys = ("mickey", "downed:", "game over", "build member", "switched actors", "exception", "CRASH")
    for c in copies:
        lines = c.since(marks[c.name])
        with open(os.path.join(OUT, f"mcoop_{c.name}.txt"), "w", encoding="utf-8") as fh:
            fh.write("\n".join(l for l in lines if any(k in l for k in keys)) + "\n")


def mcoop_end(copies, out, marks, stop, tracers, go):
    """mickey_coop --end: both games should reach the normal Continue / Load menu (state 6, Mickey branch 0 in our
    notes; logged as it goes); the host presses Continue; PASS = both games back in the field, a Sora with HP."""
    host, friend = copies
    seq, end = {c.name: [] for c in copies}, time.time() + 10
    while time.time() < end:
        for c in copies:
            g = go(c)
            s = (read_u(c, g + 0x10), read_u(c, g + 0x14, 1)) if g else None
            if not seq[c.name] or seq[c.name][-1] != s:
                seq[c.name].append(s)
        time.sleep(0.25)
    for c in copies:
        out.append(f"{c.name}: Game Over (state, Mickey branch) over 10 s: {seq[c.name]}")
        bench.screenshot(c.hwnd, os.path.join(OUT, f"mcoop_{c.name}_5_menu.png"))
    host.game.pad.run(["tap", "cross", "wait", "300"])  # Continue
    back = True
    try:
        bench.wait("both games back in the field", lambda: not go(host) and not go(friend), 60, copies)
    except bench.Failed as e:
        back = False
        out.append(f"not back: {e}")
    time.sleep(8)
    ok = back
    for c in copies:
        h = head(c)
        hp = actor_hp(c, h) if h else None
        g = go(c)
        out.append(f"{c.name}: after Continue: Game Over {'state %d' % read_u(c, g + 0x10) if g else 'no'}, HP {hp}, "
                   f"alive {c.alive()}, crash {c.crash or '-'}")
        bench.screenshot(c.hwnd, os.path.join(OUT, f"mcoop_{c.name}_6_continue.png"))
        ok &= c.alive() and not c.crash and not g and (hp or 0) > 0
    mcoop_lines(copies, marks, stop, tracers)
    return ok


CAMERA_TARGET = 0x718CB0  # actor the camera follows (mickey_sync.cpp: the loser's own Sora during the rescue)
HUD_PLAYER = 0xABCFE8  # actor the HUD's player gauge shows (same)
CMD_MENU = 0x2A10620  # YS::FIELD_COMMAND: page stack top at +0x9A8; pages of 0x80 from +0x28 (count +0x10,
# 12-byte entries at +0x14, command at +2); an actor's own command set (Mickey's) is page +0x728


def menu_cmds(c):
    """The page the command menu shows (offset in the menu object) and its commands (mickey_sync.cpp: the loser's
    should be its Sora's top page, not Mickey's own set at +0x728)."""
    fc = c.game.base + CMD_MENU
    top = read_u(c, fc + 0x9A8, 8)
    page = read_u(c, top, 8) if top else 0
    if not page:
        return 0, []
    n = read_u(c, page + 0x10)
    return page - fc, [read_u(c, page + 0x14 + i * 0xC + 2, 2) for i in range(min(n, 8))]


def mickey_coop(copies, a, out):
    """TODO 1.3 / Q6 (mickey_sync.cpp, god mode off): Mickey allowed in this room in both games (MickeyAllowed -> 1,
    appearance count 0, in memory only), the roll's winner set with KH2COOP_TEST_MICKEY_WINNER (--winner A/B). The
    friend down, then the host: both games at Game Over; only the winner's has choices; the winner picks 'I won't give
    up'; the loser's game goes back to the field (white fades in), its Sora down; the winner presses triangle until its
    Sora is up; the loser gets up with full HP and its allies back; a 10 HP hit then hurts both Soras. Each step logged;
    PASS = all of that, no crash. --end over / dies / nobody: the winner picks 'It's all over' / Mickey takes a lethal
    hit / nobody wins the roll; then both games should be at the normal Game Over and follow the host's Continue."""
    host, friend = copies
    win, lose = (host, friend) if a.winner == "A" else (friend, host)
    load_both(copies, a.slot, a.room)
    bench.both(lambda c: c.game.pad.run(["stick", "0", "1", "2500", "wait", "1500"]), copies)  # off the save point
    for c in copies:
        write_mem(c, c.game.base + MICKEY_ALLOWED, b"\xB0\x01\xC3")
        write_mem(c, c.game.base + MICKEY_COUNT, b"\x00\x00")
    out.append(f"both: Mickey allowed here, appearance count 0 (in memory only); winner set: {win.name}")
    soras = {c.name: head(c) for c in copies}
    menu_before = menu_cmds(lose)
    marks = {c.name: c.mark() for c in copies}
    stop = []
    tracers = mickey_trace(copies, stop) if a.trace else []
    dumps = {c.name: dump_sora(c, soras[c.name]) for c in copies} if a.dump else {}
    for n in range(3):
        bench.debug("A", 3, 9999)
        time.sleep(1)
        if find(friend.since(marks["B"]), "down instead of dead"):
            break
        time.sleep(3)
    out.append(f"friend down: {bool(find(friend.since(marks['B']), 'down instead of dead'))}")
    time.sleep(2)
    out.append(f"friend down: low-HP beep byte {read_u(friend, friend.game.base + LOW_HP_BEEP, 1)} (0 = silent; "
               f"party_hud.cpp red face), HP {actor_hp(friend, soras['B'])}")
    bench.screenshot(friend.hwnd, os.path.join(OUT, "mcoop_B_0_down.png"))
    bench.debug("A", 0, 9999)
    go = lambda c: read_u(c, c.game.base + GAME_OVER, 8)
    bench.wait("both Game Overs", lambda: go(host) and go(friend), 40, copies)
    time.sleep(3)
    for c in copies:
        g = go(c)
        out.append(f"{c.name}: Game Over: Mickey branch {read_u(c, g + 0x14, 1) if g else '-'}, state "
                   f"{read_u(c, g + 0x10) if g else '-'}")
    if a.end == "nobody":  # no white screen in either game
        bench.wait("both Continue / Load menus", lambda: all(go(c) and read_u(c, go(c) + 0x10) == 6 for c in copies),
                   30, copies)
        return mcoop_end(copies, out, marks, stop, tracers, go)
    bench.wait("the winner's white screen choices", lambda: go(win) and read_u(win, go(win) + 0x10) == 6, 30, copies)
    time.sleep(2)
    out.append(f"loser's Game Over state while the winner chooses: {read_u(lose, go(lose) + 0x10) if go(lose) else '-'} "
               f"(5 = held, no choices)")
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"mcoop_{c.name}_1_choice.png"))
    if a.end == "over":
        win.game.pad.run(["tap", "down", "wait", "400", "tap", "cross", "wait", "300"])  # 'It's all over'
        return mcoop_end(copies, out, marks, stop, tracers, go)
    win.game.pad.run(["tap", "cross", "wait", "300"])  # 'I won't give up'
    bench.wait("Mickey in the winner's game", lambda: head(win) not in (0, soras[win.name]), 40, copies)
    bench.wait("the loser's Game Over to end", lambda: not go(lose), 20, copies)
    time.sleep(4)
    fade = read_u(lose, lose.game.base + 0xABB290 + 0x108)
    lm = player(lose)
    stand_in = lm not in (0, soras[lose.name]) and bench.model_of(lose, lm).startswith("P_EX2")
    out.append(f"loser's own Mickey (step c, MICKEY_PUPPET): player {bench.model_of(lose, lm) if lm else '-'}, "
               f"stand-in {stand_in}")
    out.append(f"loser back: fade state {fade} (0 = faded in), own Sora {head(lose) == soras[lose.name]}, motion "
               f"{bench.motion(lose, soras[lose.name])}, HP {actor_hp(lose, soras[lose.name])}, low-HP beep byte "
               f"{read_u(lose, lose.game.base + LOW_HP_BEEP, 1)}, Donald "
               f"{bool(bench.actor_by_model(lose, 'P_EX020'))}, Goofy {bool(bench.actor_by_model(lose, 'P_EX030'))}")
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"mcoop_{c.name}_2_rescue.png"))
    if a.end == "dies":
        # What the game does when Mickey's HP reaches 0 (no Game Over within 40 s, bench 01:36): watched, not assumed.
        mickey = head(win)
        bench.debug(win.name, 0, 9999)  # the winner's list head = Mickey
        seq, start, shots, last = [], time.time(), [2, 6, 12, 20, 30, 40, 50], None
        steps = [(8, "a second hit at 0 HP", lambda: bench.debug(win.name, 0, 1)),
                 (20, "triangle x10", lambda: win.game.pad.run(["tap", "triangle", "wait", "250"] * 10))]
        while time.time() - start < 55:
            if steps and time.time() - start >= steps[0][0]:
                seq.append((round(time.time() - start, 1), steps[0][1]))
                steps.pop(0)[2]()
            g = go(win)
            s = (bench.motion(win, mickey), actor_hp(win, mickey), head(win) == mickey,
                 bench.motion(win, soras[win.name]), actor_hp(win, soras[win.name]),
                 read_u(win, g + 0x10) if g else None, bench.motion(lose, soras[lose.name]), bool(go(lose)))
            if s != last:
                seq.append((round(time.time() - start, 1), s))
                last = s
            if shots and time.time() - start >= shots[0]:
                for c in copies:
                    bench.screenshot(c.hwnd, os.path.join(OUT, f"mcoop_{c.name}_3_dies{shots[0]:02d}s.png"))
                shots.pop(0)
            for c in copies:
                c.lines()
                if c.crash or not c.alive():
                    raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'} after Mickey's 0 HP")
            time.sleep(0.2)
        out.append("after Mickey's 0 HP, (s, (Mickey motion, Mickey HP, Mickey is the player, winner's Sora motion, "
                   "its HP, winner's Game Over state, loser's Sora motion, loser at Game Over)):")
        out.extend(f"  {t}: {s}" for t, s in seq)
        mcoop_lines(copies, marks, stop, tracers)
        return False  # an observation run
    presses, start = 0, time.time()
    gaps, same_motion, samples, cam_on_sora, hud_on_sora = [], 0, 0, 0, 0
    menu_on_sora, menus = 0, set()
    while time.time() - start < 150 and head(win) != soras[win.name]:
        win.game.pad.run(["tap", "triangle", "wait", "250"])
        presses += 1
        wm, lm = head(win), player(lose)
        if stand_in and wm and lm and wm != soras[win.name] and lm != soras[lose.name]:
            try:
                wp, lp = actor_pos(win, wm), actor_pos(lose, lm)
                gaps.append(sum((x - y) ** 2 for x, y in zip(wp, lp)) ** 0.5)
                samples += 1
                same_motion += bench.motion(win, wm) == bench.motion(lose, lm)
                cam_on_sora += read_u(lose, lose.game.base + CAMERA_TARGET, 8) == soras[lose.name]
                hud_on_sora += read_u(lose, lose.game.base + HUD_PLAYER, 8) == soras[lose.name]
                m = menu_cmds(lose)
                menus.add((hex(m[0]), tuple(m[1])))
                menu_on_sora += m[0] != 0x728 and m == menu_before
            except OSError:
                pass
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'} during Mickey: {c.crash or ''}")
        if presses == 40:
            for c in copies:
                bench.screenshot(c.hwnd, os.path.join(OUT, f"mcoop_{c.name}_3_healing.png"))
            out.append(f"command menus at 10 s (top commands, slot 1): winner {menu_cmds(win)}, loser {menu_cmds(lose)}")
    out.append(f"winner: {presses} triangle presses in {time.time() - start:.0f} s, Sora back {head(win) == soras[win.name]}")
    gaps.sort()
    follow = bool(gaps) and gaps[len(gaps) // 2] < 30
    if stand_in:
        out.append(f"loser's Mickey vs the winner's: {samples} samples, distance median "
                   f"{gaps[len(gaps) // 2] if gaps else '-':.6}, max {gaps[-1] if gaps else '-':.6}, same motion "
                   f"{same_motion}/{samples}; loser's camera on its own Sora {cam_on_sora}/{samples}, HUD {hud_on_sora}/{samples}, its Sora's menu {menu_on_sora}/{samples} "
                   f"(before: {menu_before}, seen: {sorted(menus)}); loser's player "
                   f"after: {bench.model_of(lose, player(lose))}")
    seq, end = [], time.time() + 12
    while time.time() < end:  # the loser's get-up
        m = (bench.motion(lose, soras[lose.name]), actor_hp(lose, soras[lose.name]))
        if not seq or seq[-1] != m:
            seq.append(m)
        time.sleep(0.25)
    # Your request 2026-10-21: both get-ups start together. Read from the logs ('getup start' / 'control' marks).
    def first(c, text):
        l = find(c.since(marks[c.name]), text)
        return l[0][:12] if l else None
    w_up, l_up = first(win, "Healing Light: our Sora is up"), first(lose, "our Mickey left")
    out.append(f"get-up start: winner {w_up}, loser {l_up}")
    out.append(f"loser after Healing Light: (motion, HP) {seq}, Donald {bool(bench.actor_by_model(lose, 'P_EX020'))}, "
               f"Goofy {bool(bench.actor_by_model(lose, 'P_EX030'))}")
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"mcoop_{c.name}_4_after.png"))
    time.sleep(2)
    if a.dump:
        after = {c.name: dump_sora(c, soras[c.name]) for c in copies}
        dump_diff(dumps[lose.name], after[lose.name], dumps[win.name], after[win.name], out, "loser_vs_winner")
    landed = True
    for c in (lose, win):
        y0, peak, back = jump_test(c, soras[c.name])
        out.append(f"{c.name} ({'loser' if c is lose else 'winner'}) jumps: height {y0}, furthest {peak:+}, after 3 s {back:+}")
        landed &= abs(peak) > 5 and abs(back) < 5
    bench.both(lambda c: c.game.pad.run(["stick", "0", "1", "1500", "wait", "300", "tap", "cross", "wait", "500"]), copies)
    time.sleep(2)
    # A Sora brought back from 0 HP by a direct write may still count as dead to the game: does a hit still hurt?
    before = {c.name: actor_hp(c, soras[c.name]) for c in copies}
    bench.debug("A", 0, 10)
    time.sleep(1)  # one debug command per frame: a second one sent at once replaces the first
    bench.debug("A", 3, 10)
    time.sleep(2)
    hurt = {c.name: (before[c.name] or 0) > (actor_hp(c, soras[c.name]) or 0) for c in copies}
    out.append(f"a 10 HP hit after the rescue: " + ", ".join(
        f"{c.name} {before[c.name]} -> {actor_hp(c, soras[c.name])}" for c in copies))
    mcoop_lines(copies, marks, stop, tracers)
    ok = all(hurt.values()) and landed
    for c in copies:
        hp = actor_hp(c, soras[c.name])
        out.append(f"{c.name}: player is own Sora {head(c) == soras[c.name]}, HP {hp}, Game Over {'yes' if go(c) else 'no'}, "
                   f"alive {c.alive()}, crash {c.crash or '-'}")
        ok &= c.alive() and not c.crash and head(c) == soras[c.name] and (hp or 0) > 0 and not go(c)
    ok &= fade == 0 and bool(bench.actor_by_model(lose, "P_EX020"))
    ok &= stand_in and follow and samples > 0 and cam_on_sora == samples == hud_on_sora == menu_on_sora  # step c: Mickey follows, camera on Sora
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


def mickey_real(copies, a, out):
    """What the game does when a real enemy hit takes Mickey's last HP (your question 2026-10-18: does he still revive
    Sora, with less HP?). Save 20, the Twin Lords fight (07/03), Mickey allowed in both games, the host wins the roll;
    both down, the host's 'I won't give up'; Mickey left at 2 HP by a debug hit, then he stands still for the bosses.
    Logged for 120 s, and on until the friend has been up for 25 s (the bosses' knock-ups): Mickey's motion / HP, who
    is the player, both Soras, the friend's height, Game Overs; screenshots every 10 s (mreal_<copy>_<s>).
    Your rule 2026-10-18: Mickey's last HP gets the host up with half HP, the friend (the loser) stays down, its usual
    timer starts, it gets up with 26% HP, or earlier through a heal (an ally's item or Cure is fine, your answer
    2026-10-21); it must not float (back on the floor after each knock-up, a jump lands).
    PASS = no crash, the friend's release and get-up (26% by the timer, or a heal's HP), no float, the jump lands."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    bench.warp(0x07, 0x03, 59)
    bench.wait("both in 07/03", lambda: all(c.room() == (0x07, 0x03) for c in copies), 60, copies)
    time.sleep(8)
    for c in copies:
        write_mem(c, c.game.base + MICKEY_ALLOWED, b"\xB0\x01\xC3")
        write_mem(c, c.game.base + MICKEY_COUNT, b"\x00\x00")
    soras = {c.name: head(c) for c in copies}
    marks = {c.name: c.mark() for c in copies}
    for n in range(3):
        bench.debug("A", 3, 9999)
        time.sleep(1)
        if find(friend.since(marks["B"]), "down instead of dead"):
            break
        time.sleep(3)
    out.append(f"friend down: {bool(find(friend.since(marks['B']), 'down instead of dead'))}")
    bench.debug("A", 0, 9999)
    go = lambda c: read_u(c, c.game.base + GAME_OVER, 8)
    bench.wait("both Game Overs", lambda: go(host) and go(friend), 40, copies)
    bench.wait("the host's white screen choices", lambda: go(host) and read_u(host, go(host) + 0x10) == 6, 30, copies)
    time.sleep(2)
    host.game.pad.run(["tap", "cross", "wait", "300"])  # 'I won't give up'
    bench.wait("Mickey", lambda: head(host) not in (0, soras["A"]), 40, copies)
    time.sleep(4)
    mickey = head(host)
    hp0 = actor_hp(host, mickey) or 0
    if hp0 > 2:
        bench.debug("A", 0, hp0 - 2)  # the list head = Mickey: left at 2 HP
    time.sleep(1)
    out.append(f"Mickey {mickey:#x}: HP {hp0} -> {actor_hp(host, mickey)}; now the bosses' hits")
    fy = lambda: round(struct.unpack("<f", peek.read(friend.game.h, soras["B"] + 0x674, 4))[0])
    seq, start, last, shot, up_at, ys = [], time.time(), None, set(), None, []
    while time.time() - start < (120 if up_at is None else max(120, up_at + 25)) and time.time() - start < 220:
        g = go(host)
        s = (bench.motion(host, mickey), actor_hp(host, mickey), head(host) == mickey, bench.motion(host, soras["A"]),
             actor_hp(host, soras["A"]), read_u(host, g + 0x10) if g else None, bench.motion(friend, soras["B"]),
             actor_hp(friend, soras["B"]), bool(go(friend)), fy() // 10 * 10)
        if up_at is None and find(friend.since(marks["B"]), "downed: getting up with HP"):
            up_at = time.time() - start
        if up_at is not None and not go(friend):
            ys.append((round(time.time() - start, 1), fy()))
        if s != last:
            seq.append((round(time.time() - start, 1), s))
            last = s
        k = int(time.time() - start)
        if k % 10 == 0 and k not in shot:
            shot.add(k)
            for c in copies:
                bench.screenshot(c.hwnd, os.path.join(OUT, f"mreal_{c.name}_{k:03d}.png"))
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
        time.sleep(0.2)
    out.append("(s, (Mickey motion, Mickey HP, Mickey is the player, host's Sora motion, its HP, host's Game Over state, "
               "friend's Sora motion, its HP, friend at Game Over, friend's height /10)):")
    out.extend(f"  {t}: {s}" for t, s in seq[:80])
    hits = [l[13:150] for l in host.since(marks["A"]) if f"exe+0x{mickey - host.game.base:X}" in l and "HP change" in l]
    out.append(f"HP changes on Mickey: {hits[:12]}")
    keys = ("mickey", "downed:", "game over", "switched actors", "HP change: Sora", "exception", "CRASH")
    for c in copies:
        with open(os.path.join(OUT, f"mreal_{c.name}.txt"), "w", encoding="utf-8") as fh:
            fh.write("\n".join(l for l in c.since(marks[c.name]) if any(k in l for k in keys)) + "\n")
    fl = friend.since(marks["B"])
    release = find(fl, "we stay down, the usual timer starts")
    gotup = find(fl, "downed: getting up with HP")
    out.append(f"friend: release {release[0][13:] if release else 'not seen'}")
    out.append(f"friend: get-up {gotup[0][13:] if gotup else 'not seen'}")
    maxhp = read_u(friend, friend.game.base + 0x2A23598 + 4)
    want = f"getting up with HP {max(1, (maxhp * 26 + 99) // 100)}/{maxhp}"
    # Floating: up is negative y, so the floor is the largest y seen once the friend was up; a Sora left in the air
    # sits more than 30 above it for over 3 s in a row.
    floor = max((y for _, y in ys), default=None)
    high = [t for t, y in ys if floor is not None and y < floor - 30]
    stuck, run0 = 0.0, None
    for i, t in enumerate(high):
        run0 = t if i == 0 or t - high[i - 1] > 0.5 else run0
        stuck = max(stuck, t - run0)
    out.append(f"friend's height once up: floor {floor}, {len(ys)} samples, {len(high)} more than 30 above it, "
               f"longest {stuck:.1f} s (first {high[:5]})")
    landed = None
    if gotup and bench.motion(friend, soras["B"]) not in (252, 253, 54) and not go(friend):
        y0, peak, back = jump_test(friend, soras["B"])
        landed = abs(peak) > 5 and abs(back) < 5
        out.append(f"friend jumps: height {y0}, furthest {peak:+}, after 3 s {back:+}")
    else:
        out.append(f"friend jump test skipped (motion {bench.motion(friend, soras['B'])}, Game Over {bool(go(friend))})")
    healed = find(fl, "reached our Sora: getting up now")  # an ally's heal before the timer
    if healed:
        out.append(f"friend: got up through a heal before the timer: {healed[0][13:120]}")
    ok = bool(release) and bool(gotup) and (want in gotup[0] or bool(healed)) and stuck <= 3 and landed is not False
    return ok and all(c.alive() and not c.crash for c in copies)


def getup(copies, a, out):
    """Your pick 2026-10-18 (god mode off, save 52, no fight): the friend goes down off the save point; while down its
    gauge shows the KO face and the low-HP beep is off; it gets up on its own (6 s after a calm room) with 26% of max
    HP rounded up (above the game's low-HP line, max/4), beep still off. Screenshots getup_B_<step>."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    bench.both(lambda c: c.game.pad.run(["stick", "0", "1", "2500", "wait", "1500"]), copies)  # off the save point
    sora = head(friend)
    mark = friend.mark()
    beep = lambda: read_u(friend, friend.game.base + LOW_HP_BEEP, 1)
    for n in range(3):  # its own rules may keep it at 1 HP once (Second Chance)
        bench.debug("A", 3, 9999)
        time.sleep(1)
        if find(friend.since(mark), "down instead of dead"):
            break
        time.sleep(3)
    down = find(friend.since(mark), "down instead of dead")
    time.sleep(1.5)
    out.append(f"friend down: {bool(down)}, HP {actor_hp(friend, sora)}, beep byte {beep()} (0 = off)")
    bench.screenshot(friend.hwnd, os.path.join(OUT, "getup_B_1_down.png"))
    bench.wait("the friend's get-up", lambda: find(friend.since(mark), "downed: getting up with HP"), 45, [friend, host])
    time.sleep(6)
    up = find(friend.since(mark), "downed: getting up with HP")
    hp, maxhp = actor_hp(friend, sora), read_u(friend, friend.game.base + 0x2A23598 + 4)
    want = max(1, (maxhp * 26 + 99) // 100)
    out.append(f"friend up: '{up[0][13:]}', HP now {hp}/{maxhp} (26% rounded up = {want}, low-HP line {maxhp // 4}), "
               f"beep byte {beep()}")
    bench.screenshot(friend.hwnd, os.path.join(OUT, "getup_B_2_up.png"))
    return bool(down) and (hp or 0) == want and hp > maxhp // 4 and all(c.alive() and not c.crash for c in copies)


ADMIT_LIMIT, ADMIT_USED = 0x2A0F7DC, 0x2A0F830  # the actor factory's admission budget (float32 each)


def swarm(copies, a, out):
    """Your setup 2026-10-18 (Land of Dragons climb, Shan-Yu's swarm, 08/07 programs 73): both Soras invincible (the
    bench's god mode), every hit on an enemy takes all its HP (KH2COOP_TEST_ONE_HIT=1), both lock on (R1), attack
    and jump-attack until the mission ends (the room changes) or --seconds. Every 2 s in both games: the factory's
    budget (limit, used), the enemies (M_*) in the actor list, the swarm's M_EX660_RAW. Screenshots swarm_<copy>_<s>,
    the timeline in swarm_timeline.txt. PASS = no crash (the findings are in the summary)."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    bench.warp(0x08, 0x07, 73)
    bench.wait("both in 08/07", lambda: all(c.room() == (0x08, 0x07) for c in copies), 90, copies)
    time.sleep(5)
    f32 = lambda c, off: struct.unpack("<f", peek.read(c.game.h, c.game.base + off, 4))[0]
    fight = ["tap", "r1", "wait", "100"] + ["tap", "cross", "wait", "150"] * 3 + [
        "tap", "circle", "wait", "150"] + ["tap", "cross", "wait", "150"] * 2

    def sample(c):
        try:
            names = [n for _, n in bench.actors(c)]
            return (round(f32(c, ADMIT_LIMIT)), round(f32(c, ADMIT_USED)), sum(n.startswith("M_") for n in names),
                    names.count("M_EX660_RAW"))
        except OSError:
            return None

    timeline, start, shot, nxt, peak = [], time.time(), set(), 0.0, {c.name: 0 for c in copies}
    seconds, done = min(a.seconds, 300), None
    while time.time() - start < seconds and all(c.room() == (0x08, 0x07) for c in copies if c.alive()):
        if done is None and find(host.since(marks["A"]), "our mission script ended the fight"):
            done = time.time()  # mission complete (the timer ran out): 5 s more, then stop
        if done and time.time() - done > 5:
            break
        bench.both(lambda c: c.game.pad.run(fight), copies)
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
        t = time.time() - start
        if t >= nxt:
            nxt = t + 2
            row = {c.name: sample(c) for c in copies}
            for c in copies:
                if row[c.name] and row[c.name][1] > peak[c.name]:
                    peak[c.name] = row[c.name][1]
            timeline.append(f"{t:6.1f} " + " | ".join(
                f"{n} budget {r[0]} used {r[1]}, enemies {r[2]}, M_EX660_RAW {r[3]}" if r else f"{n} -"
                for n, r in row.items()))
        k = int(t) // 15 * 15
        if k not in shot:
            shot.add(k)
            for c in copies:
                bench.screenshot(c.hwnd, os.path.join(OUT, f"swarm_{c.name}_{k:03d}.png"))
    out.append(f"ran {time.time() - start:.0f} s; rooms now A {host.room()} B {friend.room()}")
    with open(os.path.join(OUT, "swarm_timeline.txt"), "w") as fh:
        fh.write("time   " + "per copy: budget limit, used, enemies (M_*) in the actor list, of them M_EX660_RAW\n")
        fh.write("\n".join(timeline) + "\n")
    out.extend(timeline[::5])
    out.append(f"peak used: A {peak['A']}, B {peak['B']}")
    fl = friend.since(marks["B"])
    made = find(fl, "no spawn record) has no match here: made")
    failed = find(fl, "no spawn record) has no match here: FAILED")
    last = [l for l in fl if "host objects made here" in l][-1:]
    lingering = {l.split()[2] for l in fl if "removed but still here" in l}
    parked = {l.split()[2] for l in fl if "has no host match; parked" in l}
    out.append(f"friend: made lines {len(made)}, failed lines {len(failed)} (logged up to 60), "
               f"last count '{last[0][13:] if last else '-'}'")
    out.append(f"friend: removed but still here {len(lingering)}, parked {len(parked)} (distinct)")
    out.append(f"host one-hit lines {len(find(host.since(marks['A']), 'TEST one hit'))}")
    keys = ("spawn:", "removed", "parked", "world: enemies host", "mission", "TEST one hit", "exception", "CRASH")
    for c in copies:
        with open(os.path.join(OUT, f"swarm_{c.name}.txt"), "w", encoding="utf-8") as fh:
            fh.write("\n".join(l for l in c.since(marks[c.name]) if any(k in l for k in keys)) + "\n")
    return all(c.alive() and not c.crash for c in copies)


VALOR_RECORD = 0x9ACBA4  # Valor's save record (level +2, EXP +4); forms 1-10 from exe+0x9ACBA4, 0x38 each


def sheet_link(c):
    """(our Sora's form +0xDE0, its stat sheet's form link +0x254 decoded, as an exe offset or 0)."""
    sora = read_u(c, c.game.base + 0x2A171C8, 8)
    if not sora:
        return None
    sheet = read_u(c, sora + 0x5C0, 8)
    link = bench.decode(c, read_u(c, sheet + 0x254)) if sheet else 0
    return read_u(c, sora + 0xDE0), (link - c.game.base) if link else 0


def valor_sheet(copies, a, out):
    """TODO 1.102 (host crash 2026-10-21: a Valor level-up with the Sora copy in the room). The host Drives into Valor
    in save 10's room (LoD 08/00), then warps to the swarm room (08/07 programs 73: enemies at once), so the copy is
    built after the Drive; its Valor EXP is set to 9999 and it locks on and attacks (one-hit kills) until Valor's level
    goes up or 60 s. Our Sora's form and its sheet's form link are read before and after the warp.
    PASS = Valor's level went up, no crash. Run with --env KH2COOP_SORA_SHEET_FIX=0 to see the bug."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    valor = lambda: (read_u(host, host.game.base + VALOR_RECORD + 2, 1), read_u(host, host.game.base + VALOR_RECORD + 4))
    out.append(f"before: form / link {sheet_link(host)}, Valor (level, EXP) {valor()} (Valor's record exe+0x{VALOR_RECORD:X})")
    host.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])
    time.sleep(6)
    out.append(f"in Valor: form / link {sheet_link(host)}")
    bench.warp(0x08, 0x07, 73)
    bench.wait("both in 08/07", lambda: all(c.room() == (0x08, 0x07) for c in copies), 90, copies)
    time.sleep(4)
    out.append(f"after the warp: form / link {sheet_link(host)}")
    start_level = valor()[0]
    write_mem(host, host.game.base + VALOR_RECORD + 4, struct.pack("<I", 9999))
    fight = ["tap", "r1", "wait", "100"] + ["tap", "cross", "wait", "150"] * 4
    start = time.time()
    while time.time() - start < 60 and valor()[0] == start_level:
        host.game.pad.run(fight)
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
    time.sleep(2)
    for c in copies:
        c.lines()
        bench.screenshot(c.hwnd, os.path.join(OUT, f"valor_sheet_{c.name}.png"))
    out.append(f"after {time.time() - start:.0f} s of attacks: Valor (level, EXP) {valor()}, form / link {sheet_link(host)}")
    for c in copies:
        lines = [l[13:200] for l in c.since(marks[c.name]) if "stat sheet" in l or "Sora sheet" in l or "CRASH" in l]
        out.append(f"{c.name} sheet lines ({len(lines)}):")
        out.extend(f"    {l}" for l in lines[:12])
    return valor()[0] > start_level and all(c.alive() and not c.crash for c in copies)


def formlook(copies, a, out):
    """TODO 1.24 research step 1 (form_look.cpp, test command 18): in save 59 (all forms but Anti) both players Drive
    into the same entry of their list (--pick = how many entries, from the top; the gauge is refilled first: stat
    sheet +0x1B1 = +0x1B2 full bars), so each game has that form's files and each copy plays the other's form motions
    (as in play); each game's copy takes the form's look, then each player attacks in turn; screenshots
    formlook_<n>_<step>_<copy>.png; then both copies go back (form 0) and both Revert. PASS = no crash (the look is
    judged from the screenshots).
    --one (step 2 of the plan, files on demand): only one player Drives (--by A = the host, B = the friend), so the
    other game doesn't have the form's files; its copy asks for them and takes the look once they're in (the time is
    logged). Screenshots 4 s later, after the Drive's shine. PASS = no crash and every look applied.
    --mixed: both Drive, the friend into the next entry (different forms at once).
    --auto (step 3): no test commands; each copy follows the other player's body from the pose packet (form start
    and Revert). Screenshots 1 s and 4 s after the Drive. PASS = no crash, a look per Drive and one back per Revert."""
    host, friend = copies
    drivers = copies if not a.one else [host] if a.by == "A" else [friend]
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    form_now = lambda c: read_u(c, c.game.base + 0x9ACDD4, 1)

    def refill(c):
        sheet = read_u(c, read_u(c, c.game.base + 0x2A171C8, 8) + 0x5C0, 8)
        write_mem(c, sheet + 0x1B0, bytes([100, read_u(c, sheet + 0x1B2, 1)]))

    attack = ["tap", "cross", "wait", "250"] * 3
    tried = 0
    for n in range(a.pick or 5):
        for c in copies:
            refill(c)
        # --mixed: the friend picks the next entry, so each copy shows a form its game isn't in (files on demand while
        # its own Sora is in another form; then both Revert, which drops the Drive area's files).
        pick = lambda c: (n + 1) % (a.pick or 5) if a.mixed and c is friend else n
        bench.both(lambda c: c.game.pad.run(DRIVE + ["tap", "cross", "wait", "800"] + ["tap", "down", "wait", "300"] *
                                            pick(c) + ["tap", "cross", "wait", "500"]), drivers)
        if a.auto:  # just after the body swap (~0.67 s after the form change)
            time.sleep(1.2)
            for c in copies:
                bench.screenshot(c.hwnd, os.path.join(OUT, f"formlook_{n}_early_{c.name}.png"))
            time.sleep(3.8)
        else:
            time.sleep(5)
        forms = {c.name: form_now(c) for c in drivers}
        out.append(f"drive list entry {n}: forms now {forms}")
        if not all(forms.values()):
            continue
        tried += len(drivers)
        if not a.auto:
            for c in drivers:
                # Each game's copy is the other player: it takes the form of whoever drove in the other game.
                seer = friend if c is host else host
                bench.debug(seer.name, 18, forms[c.name])
        time.sleep(1.5 if not a.one and not a.auto else 0 if a.auto else 4)
        for c in copies:
            bench.screenshot(c.hwnd, os.path.join(OUT, f"formlook_{n}_idle_{c.name}.png"))
        for doer, seer in ((friend, host), (host, friend)):
            doer.game.pad.run(attack[:4])
            bench.screenshot(seer.hwnd, os.path.join(OUT, f"formlook_{n}_attack_{seer.name}.png"))
            doer.game.pad.run(attack[4:])
            time.sleep(1.5)
        if not a.auto:
            for c in drivers:
                bench.debug((friend if c is host else host).name, 18, 0)
        time.sleep(1.5)
        bench.both(lambda c: c.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"]),
                   drivers)  # Revert
        time.sleep(5)
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
    for c in copies:
        c.lines()
        lines = [l[13:200] for l in c.since(marks[c.name]) if "form look" in l or "CRASH" in l]
        out.append(f"{c.name} form look lines ({len(lines)}):")
        out.extend(f"    {l}" for l in lines[:30])
    looks = [l for c in copies for l in c.since(marks[c.name]) if "now looks like" in l]
    applied = sum("form 0" not in l for l in looks)
    back = len(looks) - applied
    out.append(f"looks applied: {applied} (back to Sora: {back}), Drives that worked: {tried}")
    ok = applied >= tried and (not a.auto or back >= tried)
    return all(c.alive() and not c.crash for c in copies) and (ok or not (a.one or a.auto))


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


SCENARIOS = {"ship": ship, "alone": alone, "downed": downed, "forms": forms, "limit": limit, "summon": summon, "party": party, "companions": companions, "companion_ko": companion_ko, "mp_probe": mp_probe, "xemnas": xemnas, "xend": xend, "xlimit": xlimit, "xcontinue": xcontinue, "menu": menu, "hud": hud, "hud2": hud2, "item": item, "costume": costume, "inspect": inspect, "heal": heal, "mickey": mickey, "mickey_coop": mickey_coop, "getup": getup, "mickey_real": mickey_real, "swarm": swarm, "valor_sheet": valor_sheet, "formlook": formlook}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scenario", choices=sorted(SCENARIOS))
    ap.add_argument("--slot", type=int, default=52)  # HB Borough 04/06, Donald + Goofy
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--host-only", action="store_true")  # xend: only the host attacks (the friend's hits can't kill a busy boss)
    ap.add_argument("--by", choices=("A", "B"), default="A")  # limit/summon/party: who opens the menu
    ap.add_argument("--other-tries", action="store_true")  # limit: the other player tries a Limit meanwhile
    ap.add_argument("--what", choices=("form", "summon"), default="form")
    ap.add_argument("--overlap", action="store_true")  # companions: the other player uses a form meanwhile  # companions: a Drive Form or a summon
    ap.add_argument("--pick", type=int, default=0)  # forms/summon/limit: which one in the list (0 = the first)
    ap.add_argument("--ship", action="store_true")  # xemnas/xlimit: through the ship-fight room first
    ap.add_argument("--hp", action="store_true")  # xemnas: keep the party HP full by writing it (for GOD=0)
    ap.add_argument("--motions", action="store_true")  # xemnas: log every motion change of each Xemnas
    ap.add_argument("--shots", action="store_true")  # xemnas: both games every 3 s after the hit; limit: both games
    ap.add_argument("--boss-hp", type=int, default=200)  # xend: every boss's HP at the start
    ap.add_argument("--seconds", type=int, default=420)  # xend: give up after this long
    ap.add_argument("--fx-census", action="store_true")  # xemnas: live effects and their particles every 1 s
    ap.add_argument("--winner", choices=("A", "B"), default="A")  # mickey_coop: who wins the roll (test setting)
    ap.add_argument("--end", choices=("rescue", "over", "dies", "nobody"), default="rescue")  # mickey_coop: how it ends
    ap.add_argument("--dump", action="store_true")  # mickey_coop: both Soras' actor / movement bytes before and after
    ap.add_argument("--trace", action="store_true")  # mickey: fades and Game Over flags every 50 ms (mickey_trace_<copy>.txt)
    ap.add_argument("--friend-back", action="store_true")  # mickey: the friend's game takes the Mickey branch, no Mickey
    ap.add_argument("--one", action="store_true")  # formlook: only one player Drives (--by), files on demand
    ap.add_argument("--auto", action="store_true")
    ap.add_argument("--mixed", action="store_true")  # formlook: the friend Drives into the next entry  # formlook: no test commands, the copy follows the pose
    ap.add_argument("--low", type=int, default=5)  # heal: the HP a Sora is held at (a quarter of max or less)
    a = ap.parse_args()
    saves = {}
    with open(os.path.join(bench.ROOT, "build", "bench", "coop_saves.txt")) as fh:
        for line in fh:
            s, w, r = line.split()
            saves[int(s)] = (int(w, 16), int(r, 16))
    if a.scenario == "formlook" and a.slot == 52:
        a.slot = 59  # all Drive Forms but Anti
    if a.scenario in ("swarm", "valor_sheet") and a.slot == 52:
        a.slot = 10  # Land of Dragons
    if a.scenario in ("limit", "heal", "mickey_real") and a.slot == 52:
        a.slot = 20  # a fight (see limit)
    a.room = saves[a.slot]
    os.makedirs(OUT, exist_ok=True)
    env = (["KH2COOP_GOD=0"] if a.scenario in ("downed", "hud2", "item", "costume", "xcontinue", "mickey", "mickey_coop",
                                               "getup", "mickey_real") else []) + (
        ["KH2COOP_HEAL_TRACE=1", "KH2COOP_ENEMY_GOD=1"] if a.scenario == "heal" else []) + (
        ["KH2COOP_HEAL_TRACE=1", "KH2COOP_GOD=0"] if a.scenario == "companion_ko" else []) + (
        ["KH2COOP_ENEMY_GOD=1"] if a.scenario == "mickey_real" else []) + (
        ["KH2COOP_TEST_ONE_HIT=1"] if a.scenario in ("swarm", "valor_sheet") else []) + (
        [f"KH2COOP_TEST_MICKEY_WINNER={'nobody' if a.end == 'nobody' else 'host' if a.winner == 'A' else 'friend'}"]
        if a.scenario in ("mickey_coop", "mickey_real") else []) + a.env
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
