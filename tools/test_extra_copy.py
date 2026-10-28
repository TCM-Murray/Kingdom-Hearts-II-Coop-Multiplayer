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
  keyblade TODO 3.3 / 1.110 research, read only: each game's Keyblade items in the save, both Soras' weapon actors
           (object, group, the file entries they use) and every listed weapon file with its group.
  blade    TODO 3.3 / 1.110 (copy_blade.cpp): Keyblade changes in the pause menu (the friend, then the host twice, the
           second freeing files the host's copy shares), then the friend's form Keyblades in its Drives; each copy
           must take the other player's Keyblade, no crash; --forms-only: only the Drives.
  gym_limit  Track 2: the gym (02/28, no enemies, save 59, its Limit abilities equipped in memory), the forced battle
           state in both games (debug battle on), the --by player's first Limit (--pick), then battle off: state 2
           held, the Limit runs, 0 after, no crash.
  saferead docs/CRASH_RISK_REVIEW.md section 4 (safe_read.cpp): the saferead test command in both games (100 faulting
           reads, our guard page, another thread's stack guard page); both still running 20 s later, no BUG line.
  form_share  TODO 1.127 (save 59): for each form and both orders, one player Drives, the other into the same form, the
           first Reverts while the second stays in (the friend's crash at Data Roxas, F342); no crash.
  file_groups  the review's 5.2.6 research, read only (save 59): both games' file lists by group at the room load, each
           player's Drive and Revert, the host's menu swap and a room change; new, gone and moved negative / 5100-5199 files.
  neg_group  the review's 5.2.6 write test (save 59, 'negfiles' test command): a look's files in a negative group of
           ours through Drives, Reverts, the menu swap, a summon, a Limit, a spawn and the room change; nothing of
           ours may move or join another group. --decline: a budget too small. --scene: the Pete walk-in (save 11).
  limit_gap  F356's repro (TODO 1.127, save 59 in the gym, Limits equipped in memory): one player Drives into entry n,
           the other into n + 1, the first Reverts: the game's Limit preload lays the heap out with no load wait; both
           ways, n = 0..--pick-1 (default 2); 'limitgap' first proves the preload runs. PASS = no crash, every case set
           up, no file the copy wears moved by the preload.
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


def special(copies, a, out, name, keys, picks=None, warp=None, steps=4, during=None, prep=None, loaded=None):
    load_both(copies, a.slot, a.room)
    if loaded:  # e.g. equip the Limit abilities before the warp's room load builds the ability bits (gym_limit)
        loaded()
    if warp:  # (world, room, program): e.g. into a fight
        bench.warp(*warp)
        bench.wait(f"both in {warp[0]:02X}/{warp[1]:02X}", lambda: all(c.room() == warp[:2] for c in copies), 60, copies)
        time.sleep(12)
    if prep:  # e.g. the forced battle state (gym_limit)
        prep()
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
        # TODO 1.108: after the friend's Limit the host's companions come back (the bring-back kept Goofy away while
        # limit sync's 'other Limit' flag was still set; two-PC run 2026-10-22)
        time.sleep(4)
        kept = find(other.since(marks[other.name]), "Limit holds it")
        party = party_now(other)
        out.append(f"{other.name}: places kept away for the friend's Limit after it ended {len(kept)}, Donald / Goofy "
                   f"here {party}")
        ok = ok and not kept
    return ok


BATTLE = 0x2A11404  # u32 battle state: 0 calm, 1 a fight, 2 forced (VERIFIED_OFFSETS L132)
RECORDS = 0x9ABDA0  # character records, 0x114 bytes each from Sora (id 1); +0x54 80 abilities, top bit = equipped
LIMIT_ABILITIES = range(0xC6, 0xD4)  # Trinity Limit 0xC6, Donald's 0xC7/0xC8, Goofy's 0xC9/0xCA, the allies' up to 0xD3


def battle_state(c):
    return struct.unpack("<I", peek.read(c.game.h, c.game.base + BATTLE, 4))[0]


def equip_limits(c, chars=(1, 2, 3)):
    """Equips every Limit ability the character records hold, in memory only (save 59 has them all off; the stat
    sheet's ability bits come from these records at the next room load). Returns how many it switched on."""
    n = 0
    for cid in chars:
        at = c.game.base + RECORDS + 0x114 * (cid - 1) + 0x54
        ab = list(struct.unpack("<80H", peek.read(c.game.h, at, 160)))
        for i, v in enumerate(ab):
            if (v & 0x7FFF) in LIMIT_ABILITIES and not v & 0x8000:
                ab[i] = v | 0x8000
                n += 1
        write_mem(c, at, struct.pack("<80H", *ab))
    return n


def gym_limit(copies, a, out):
    """Track 2: a Limit with no enemies. The gym (Betwixt and Between 02/28, programs 0/0/0, Donald + Goofy), the
    forced battle state in both games (debug battle on), the --by player's first Limit from the menu; then battle
    off. Save 59 has every Limit ability unequipped: they are equipped in memory before the warp. Expected: the state 2 while held, the Limit runs and syncs (limit sync lines in both), 0 after off, no crash."""
    marks = {c.name: c.mark() for c in copies}
    user = copies[1] if a.by == "B" else copies[0]
    states = {}

    def prep():
        states["before"] = {c.name: battle_state(c) for c in copies}
        for c in copies:
            bench.debug(c.name, 20, 1)
        time.sleep(1.5)
        states["on"] = {c.name: battle_state(c) for c in copies}

    def loaded():
        out.append(f"Limit abilities equipped (memory only): {[equip_limits(c) for c in copies]}")

    ok = special(copies, a, out, "gym_limit", [], ["tap", "down", "wait", "300"] * a.pick, warp=(0x02, 0x28, 0, 0),
                 steps=6, prep=prep, loaded=loaded)
    states["after Limit"] = {c.name: battle_state(c) for c in copies}
    for c in copies:
        bench.debug(c.name, 20, 0)
    time.sleep(1.5)
    states["off"] = {c.name: battle_state(c) for c in copies}
    time.sleep(3)
    states["off +3 s"] = {c.name: battle_state(c) for c in copies}
    out.append(f"battle state: {states}")
    started = find(user.since(marks[user.name]), "type 0x51)")
    out.append(f"{user.name}: Limit commands {len(started)}")
    for c in copies:
        lines = c.since(marks[c.name])
        out.append(f"{c.name}: battle test lines {[l[13:120] for l in lines if 'battle test' in l or 'debug command: battle' in l]}")
        out.append(f"{c.name}: limit sync lines {[l[13:150] for l in lines if 'limit sync:' in l][:6]}")
    held = all(v == 2 for v in states["on"].values()) and all(v == 2 for v in states["after Limit"].values())
    released = all(v == 0 for v in states["off +3 s"].values())
    return ok and held and released and bool(started) and all(c.alive() and not c.crash for c in copies)


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
    bench.warp(FINAL[0], FINAL[1], a.program)  # 74 story Final Xemnas, 98 Data Xemnas's second part
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


class DriveKeeper:
    """Infinite Drive gauge (your ask 2026-10-26, for the summon and Limit benches): keeps our own Sora's Drive gauge
    full in each copy by writing it every 0.2 s (stat slot exe+0x2A23598: +0x1B0 the bar's fill, 100 = full; +0x1B1
    and +0x1B2 the bars and the most, lead L96, which is which unknown: both set to the larger). Logs the first
    values seen and counts the refills, so the run says whether L96's fields are the gauge."""

    def __init__(self, copies):
        import ctypes
        import threading
        self.k32, self.stop_flag, self.handles = ctypes.windll.kernel32, False, []
        self.k32.OpenProcess.restype = ctypes.c_void_p
        self.first, self.refills = {}, {c.name: 0 for c in copies}
        for c in copies:
            h = self.k32.OpenProcess(0x0008 | 0x0010 | 0x0020, False, c.game.pid)  # VM_OPERATION | READ | WRITE
            self.handles.append((c.name, ctypes.c_void_p(h), c.game.base))
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        import ctypes
        buf, done = ctypes.create_string_buffer(3), ctypes.c_size_t()
        while not self.stop_flag:
            for name, h, base in self.handles:
                at = ctypes.c_void_p(base + 0x2A23598 + 0x1B0)
                if not self.k32.ReadProcessMemory(h, at, buf, 3, ctypes.byref(done)):
                    continue
                fill, b1, b2 = buf.raw
                self.first.setdefault(name, (fill, b1, b2))
                most = max(b1, b2)
                if 0 < most <= 9 and (fill < 100 or b1 != most or b2 != most):
                    self.k32.WriteProcessMemory(h, at, bytes([100, most, most]), 3, ctypes.byref(done))
                    self.refills[name] += 1
            time.sleep(0.2)

    def report(self):
        return f"first seen (fill, +0x1B1, +0x1B2) {self.first}, refills {self.refills}"

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
            # Data Xemnas holds Sora higher up: Riku jumps first (you, 2026-10-22), Rescue in the air
            host.game.pad.run(["tap", "circle", "wait", "250", "tap", "triangle", "wait", "150", "tap", "triangle",
                               "wait", "300"])
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


def form_share(copies, a, out):
    """TODO 1.127 (F342; the friend's crash 2026-10-26 19:55:18 at exe+0x3C859E, both in Limit Form): save 59, for each
    Drive list entry (--pick = how many, default 5) and both orders: the first player Drives, then the second into the
    same form (its own Keyblade then runs from files the copy's look loaded in its game), then the first Reverts while
    the second stays in: the second's game lets go of the copy's look and must keep the files its own Sora uses. Then
    the second Reverts. Pose packets drive the looks (as in play). PASS = no crash, no game closed."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    form_now = lambda c: read_u(c, c.game.base + 0x9ACDD4, 1)

    def refill(c):
        sheet = read_u(c, read_u(c, c.game.base + 0x2A171C8, 8) + 0x5C0, 8)
        write_mem(c, sheet + 0x1B0, bytes([100, read_u(c, sheet + 0x1B2, 1)]))

    def drive(c, n):
        c.game.pad.run(DRIVE + ["tap", "cross", "wait", "800"] + ["tap", "down", "wait", "300"] * n +
                       ["tap", "cross", "wait", "500"])

    def revert(c):
        c.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])

    def check(label):
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'} ({label}): {c.crash or ''}")

    cases = 0
    for n in range(a.pick or 5):
        for first, second in ((host, friend), (friend, host)):
            for c in copies:
                refill(c)
            drive(first, n)
            time.sleep(5)
            drive(second, n)
            time.sleep(5)
            forms = (form_now(first), form_now(second))
            check(f"entry {n}, {first.name} then {second.name} in")
            revert(first)
            time.sleep(5)
            check(f"entry {n}, {first.name} out, {second.name} still in")
            after = form_now(second)
            revert(second)
            time.sleep(5)
            check(f"entry {n}, both out")
            both_in = forms[0] and forms[0] == forms[1]
            cases += bool(both_in and after)
            out.append(f"entry {n}: {first.name} then {second.name}: forms {forms}, {second.name} after the other's "
                       f"Revert {after}, {'tested' if both_in and after else 'NOT the case (forms differ or none)'}")
    for c in copies:
        new = c.since(marks[c.name])
        kept = [l[13:200] for l in new if "used by another actor" in l or "let go of object" in l]
        out.append(f"{c.name}: alive {c.alive()}, crash {c.crash or '-'}, drop lines {len(kept)}:")
        out.extend(f"    {l}" for l in kept[:24])
    out.append(f"cases with both in the same form when the first Reverted: {cases} of {2 * (a.pick or 5)}")
    return all(c.alive() and not c.crash for c in copies) and cases > 0


def limit_gap(copies, a, out):
    """F356's repro (TODO 1.127; the host's crash 2026-10-27 00:25:09, Data Larxene): save 59 in the gym, for the Drive
    list entries 0..--pick-1 (default 2) and both ways: the reverter Drives into entry n, the other player into entry
    n + 1 (so the reverter's copy asks that look's files into the Drive area after the reverter's own form's), then the
    reverter Reverts: the Revert leaves its form's files in 5000, and on the next frame the game's Limit preload drops
    them, asks the Limits and lays the heap out with no load wait, moving the copy's files. Then the other Reverts.
    The preload only runs for usable Limits: save 59 has every Limit ability off, so they are equipped in memory and the
    gym's room load (02/28, Donald + Goofy) builds the ability bits; 'limitgap' with nobody in a form must show the
    preload's drop (the baseline), else the rest proves nothing.
    PASS = no crash, every case set up (the look worn, the preload's drop after the Revert), and no file the copy wears
    moved by the preload's layout (a 'moved the copy's' line or copy_blade's 'a file layout moves its file')."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    out.append(f"Limit abilities equipped (memory only): {[equip_limits(c) for c in copies]}")
    bench.warp(0x02, 0x28, 0)
    bench.wait("both in 02/28", lambda: all(c.room() == (0x02, 0x28) for c in copies), 60, copies)
    time.sleep(12)
    marks = {c.name: c.mark() for c in copies}
    for c in copies:
        bench.debug(c.name, 25, 0)
    time.sleep(2.5)
    base = {c.name: sum("caller exe+0x3e772e" in l for l in c.since(marks[c.name])) for c in copies}
    out.append(f"baseline (nobody in a form): the game's preload drops after limitgap {base}")
    if not all(base.values()):
        raise bench.Failed("the Limit preload didn't run after limitgap (no usable Limit?)")
    form_now = lambda c: read_u(c, c.game.base + 0x9ACDD4, 1)

    def refill(c):
        sheet = read_u(c, read_u(c, c.game.base + 0x2A171C8, 8) + 0x5C0, 8)
        write_mem(c, sheet + 0x1B0, bytes([100, read_u(c, sheet + 0x1B2, 1)]))

    def drive(c, n):
        c.game.pad.run(DRIVE + ["tap", "cross", "wait", "800"] + ["tap", "down", "wait", "300"] * n +
                       ["tap", "cross", "wait", "500"])

    def revert(c):
        c.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])

    def check(label):
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'} ({label}): {c.crash or ''}")

    cases, moved_all = 0, 0
    total = a.pick or 2
    for n in range(total):
        for rev, other in ((host, friend), (friend, host)):
            for c in copies:
                refill(c)
            drive(rev, n)
            time.sleep(5)
            m = rev.mark()
            drive(other, n + 1)
            time.sleep(6)
            check(f"entry {n}: {rev.name} in, {other.name} into entry {n + 1}")
            forms = (form_now(rev), form_now(other))
            worn = any("now looks like" in l and "form 0" not in l for l in rev.since(m))
            m = rev.mark()
            revert(rev)
            time.sleep(5)
            check(f"entry {n}: {rev.name} reverted with its copy in form {forms[1]}")
            new = rev.since(m)
            preload = sum("caller exe+0x3e772e" in l for l in new)
            moved = [l[13:200] for l in new if "moved the copy's" in l or "moves its file" in l]
            moved_all += len(moved)
            ok_case = forms[0] and forms[1] and forms[0] != forms[1] and worn and preload
            cases += bool(ok_case)
            out.append(f"entry {n}: {rev.name} in form {forms[0]}, {other.name} in form {forms[1]}, {rev.name}'s copy "
                       f"wore it {worn}; after {rev.name}'s Revert: the preload's drops {preload}, the copy's files "
                       f"moved {len(moved)}{'' if ok_case else ' (NOT the case)'}")
            out.extend(f"    {l}" for l in moved[:8])
            revert(other)
            time.sleep(5)
            check(f"entry {n}: both out")
    for c in copies:
        new = c.since(marks[c.name])
        lines = [l[13:200] for l in new if "limit gap:" in l or "caller exe+0x3e772e" in l or "moved the copy's" in l
                 or "moves its file" in l or "CRASH" in l]
        out.append(f"{c.name}: alive {c.alive()}, crash {c.crash or '-'}, lines ({len(lines)}):")
        out.extend(f"    {l}" for l in lines[:40])
    out.append(f"cases set up: {cases} of {2 * total}; the copy's files moved by the preload: {moved_all}")
    return all(c.alive() and not c.crash for c in copies) and cases == 2 * total and moved_all == 0


def file_groups(copies, a, out):
    """The review's 5.2.6 research, read only (no fighting): a file group the Drive layout doesn't move. Save 59; the file
    list of both games dumped by group at: the room load, the host's Drive (--pick entry), its Revert, the friend's
    Drive, its Revert, the host's pause-menu Keyblade swap, and after a room change (the gym). Each dump: every entry
    (file_groups_<copy>.txt) and per class (negative groups, 5100-5199, the Drive area 5000-5099, let go >= 10000, the
    rest) the count and bytes; for the negative and 5100-5199 entries, which ones are new, gone or moved since the last
    dump. Also the heap's end (*(exe+0x749740)) and where the negative files begin (exe+0x29F33F0). PASS = no crash."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    q = lambda c, addr: struct.unpack("<Q", peek.read(c.game.h, addr, 8))[0]
    last = {c.name: {} for c in copies}
    full = {c.name: [] for c in copies}

    def kind(g):
        return ("negative" if g < 0 else "let go" if g >= 10000 else "5100-5199" if 5100 <= g < 5200
                else "Drive 5000-5099" if 5000 <= g < 5100 else "rest")

    def dump(moment):
        for c in copies:
            files = file_list(c)
            end, neg = q(c, c.game.base + 0x749740), q(c, c.game.base + 0x29F33F0)
            top = max((pl + sz for _, _, _, g, pl, sz, _ in files if 0 <= g < 10000), default=0)
            full[c.name].append(f"== {moment}: {len(files)} entries; heap end {end:#x}, negative files from {neg:#x}, "
                                f"top of the rest {top:#x}, room between {neg - top}")
            full[c.name] += [f"   {n} group {g} state {s} place {pl:#x} size {sz:#x} flags {f:#x}"
                             for _, n, s, g, pl, sz, f in files]
            classes = {}
            for _, n, s, g, pl, sz, f in files:
                k = classes.setdefault(kind(g), [0, 0])
                k[0] += 1
                k[1] += sz
            out.append(f"{c.name} {moment}: " + ", ".join(f"{k} {v[0]} ({v[1] // 1024} KB)" for k, v in sorted(classes.items()))
                       + f"; room between the rest and the negative files {(neg - top) // 1024} KB")
            now = {(n, g): pl for _, n, _, g, pl, _, _ in files if g < 0 or 5100 <= g < 5200}
            before = last[c.name]
            for key, pl in sorted(now.items(), key=lambda kv: kv[0][1]):
                if key not in before:
                    out.append(f"   new   {key[0]} group {key[1]} place {pl:#x}")
                elif before[key] != pl:
                    out.append(f"   MOVED {key[0]} group {key[1]} {before[key]:#x} -> {pl:#x}")
            out.extend(f"   gone  {k[0]} group {k[1]}" for k in before if k not in now)
            last[c.name] = now

    def check(label):
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'} ({label}): {c.crash or ''}")

    def drive(c):
        c.game.pad.run(DRIVE + ["tap", "cross", "wait", "800"] + ["tap", "down", "wait", "300"] * a.pick +
                       ["tap", "cross", "wait", "500"])

    def revert(c):
        c.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])

    form_now = lambda c: read_u(c, c.game.base + 0x9ACDD4, 1)
    dump("room load")
    for who in (host, friend):
        drive(who)
        time.sleep(5)
        check(f"{who.name}'s Drive")
        dump(f"{who.name}'s Drive (form {form_now(who)})")
        revert(who)
        time.sleep(5)
        check(f"{who.name}'s Revert")
        dump(f"{who.name}'s Revert")
    host.game.pad.run(MENU_FIRST_WEAPON)
    time.sleep(4)
    check("the menu swap")
    dump("the host's menu swap")
    bench.warp(0x02, 0x28, 0)
    bench.wait("both in the gym", lambda: all(c.room() == (0x02, 0x28) for c in copies), 60, copies)
    time.sleep(6)
    check("the room change")
    dump("room change (gym 02/28)")
    for c in copies:
        with open(os.path.join(OUT, f"file_groups_{c.name}.txt"), "w", encoding="utf-8") as fh:
            fh.write("\n".join(full[c.name]) + "\n")
    return all(c.alive() and not c.crash for c in copies)


def neg_group(copies, a, out):
    """The review's 5.2.6, your pick B, write test (form_look.cpp test command 24 'negfiles', no fighting): a look's
    files asked into a negative group of ours, then the game's own file events, and nothing of ours may move.
    Main run (save 59): the request sent during the warp to the gym must be refused (the gate: the room's files not
    all placed); in the gym both games ask Valor's look (-100..-98), then Wisdom's (-97..-95); then the host Drives
    into each Drive list entry and Reverts (the Valor and Wisdom ones: our request first, then the game's own Drive
    asks the same files: crashes, L149, so only entries 2 and 3), the friend once, the host's pause-menu Keyblade swap, a summon and its dismissal twice, a Dusk spawned
    in front of the host (invincible: KH2COOP_ENEMY_GOD=1, set here), a Limit (forced battle state); after each, both games' file lists are read: our
    files' places, our bottom (exe+0x29F33F0) and the heap's end (*(exe+0x749740)) must be unchanged. Then the room
    change (back to 04/06): ours let go at the room's own drop, before its layout, no negative file left after it.
    Then the other order in the new room: the host Drives into Valor first, then asks Valor's look.
    --decline: KH2COOP_NEG_BUDGET_KB=100 (set here): the request is declined with its line, its files let go, nothing
    moved. --shared: the no-join rule, the shared form in both orders (Valor's look, then each game's own Drive into
    Valor; each game's own Drive into Wisdom, then Wisdom's look): no crash, no join, ours placed from the heap's end
    and the game's below them. --scene: save 11, the warp to the Mysterious Tower (02/19, programs 2/0/1), Valor's look asked, the host
    teleported into the walk-in area (type-3 group, the Pete scene 802); our files watched through the scene to its
    reload. PASS = no crash, no refusal once settled, no file of ours joined another group, nothing of ours moved
    (DLL 'MOVED' lines and the list reads), ours let go at the room's drop, no 'BUG' line;
    the event block (5.2.6 step 4, KH2COOP_EVENT_BLOCK_GUARD=1 set here): each event's block served from ours and
    given back swept (no resource object left in it) with the stack index and the heap's end as at the take. The
    Drive gauge is kept full through the run (DriveKeeper); the joins of the shared
    order are reported (they decide step 4's first question)."""
    host, friend = copies
    q = lambda c, addr: struct.unpack("<Q", peek.read(c.game.h, addr, 8))[0]
    form_now = lambda c: read_u(c, c.game.base + 0x9ACDD4, 1)
    marks = {c.name: c.mark() for c in copies}
    problems = []
    state = {}
    held = {c.name: set() for c in copies}  # forms whose looks this game holds in our groups (the guard below)

    def check(label):
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'} ({label}): {c.crash or ''}")

    def no_join(c):
        return any("neg files: no-join rule on" in l for l in c.since(0))

    def placed_right(label):
        """Ours from the heap's end down to our bottom, every other placed file under 10000 below our bottom."""
        for c in copies:
            bottom, end = q(c, c.game.base + 0x29F33F0), q(c, c.game.base + 0x749740)
            wrong = [f"{n} group {g} {pl:#x}+{sz:#x}" for e, n, s, g, pl, sz, f in file_list(c)
                     if pl and ((g < 0 and not bottom <= pl < end) or (0 <= g < 10000 and pl + sz > bottom))]
            out.append(f"  {c.name} places after {label}: {'all right' if not wrong else 'WRONG ' + str(wrong[:6])}")
            if wrong:
                problems.append(f"{c.name} {label}: files out of their place {wrong[:3]}")

    def ours(c):
        return {(e, n): (g, pl, s) for e, n, s, g, pl, sz, f in file_list(c) if g < 0}

    def snap(c):
        return {"end": q(c, c.game.base + 0x749740), "bottom": q(c, c.game.base + 0x29F33F0), "ours": ours(c)}

    def baseline():
        for c in copies:
            state[c.name] = snap(c)

    def verify(label, new_ok=False):
        for c in copies:
            was, now = state[c.name], snap(c)
            moved = [f"{k[1]} {v[1]:#x} -> {now['ours'][k][1]:#x}" for k, v in was["ours"].items()
                     if k in now["ours"] and now["ours"][k][1] != v[1]]
            gone = [k[1] for k in was["ours"] if k not in now["ours"]]
            added = [k[1] for k in now["ours"] if k not in was["ours"]]
            bad = []
            if moved:
                bad.append(f"MOVED {moved}")
            if gone:
                bad.append(f"gone {gone}")
            if added and not new_ok:
                bad.append(f"new negative files {added}")
            if now["end"] != was["end"]:
                bad.append(f"heap end {was['end']:#x} -> {now['end']:#x}")
            if now["bottom"] != was["bottom"] and not new_ok:
                bad.append(f"bottom {was['bottom']:#x} -> {now['bottom']:#x}")
            out.append(f"  {c.name} after {label}: {len(now['ours'])} negative files, "
                       + ("; ".join(bad) if bad else "nothing moved"))
            if bad:
                problems.append(f"{c.name} {label}: {'; '.join(bad)}")
            state[c.name] = now

    def negfiles(c, form, timeout=8):
        m = c.mark()
        bench.debug(c.name, 24, form)
        end = time.time() + timeout
        while time.time() < end:
            lines = [l for l in c.since(m) if "neg files:" in l]
            done = [l for l in lines if "refused" in l or "DECLINED" in l or "files of ours" in l]
            if done:
                return lines, done[-1]
            time.sleep(0.2)
        return [l for l in c.since(m) if "neg files:" in l], ""

    def ask(c, form, expect_join=False):
        lines, result = negfiles(c, form)
        joins = [l for l in lines if "JOINED" in l]
        out.append(f"  {c.name} negfiles {form}: {result[13:260] if result else 'NO ANSWER'}")
        out.extend(f"      {l[13:200]}" for l in lines if "asked in group" in l)
        if "files of ours" not in result:
            problems.append(f"{c.name} negfiles {form}: {result[13:120] or 'no answer'}")
        else:
            held[c.name].add(form)
        if joins and not expect_join:
            problems.append(f"{c.name} negfiles {form}: {len(joins)} files JOINED another group")
        return joins

    def all_loaded(label, timeout=15):
        end = time.time() + timeout
        while time.time() < end:
            if all(all(v[2] == 3 for v in ours(c).values()) for c in copies):
                return
            time.sleep(0.3)
        problems.append(f"{label}: not all of our files loaded in {timeout} s: "
                        + ", ".join(f"{c.name} {[(k[1], v[2]) for k, v in ours(c).items() if v[2] != 3]}" for c in copies))

    def refill(c):
        sheet = read_u(c, read_u(c, c.game.base + 0x2A171C8, 8) + 0x5C0, 8)
        write_mem(c, sheet + 0x1B0, bytes([100, read_u(c, sheet + 0x1B2, 1)]))

    def drive(c, n):
        form = ENTRY_FORM_59[n]
        if form in held[c.name] and not no_join(c):  # L149: its request would join our entries and crash
            raise bench.Failed(f"refused by the bench: {c.name}'s own Drive into form {form}, whose look its game holds "
                               "in our groups, without the no-join rule")
        refill(c)
        c.game.pad.run(DRIVE + ["tap", "cross", "wait", "800"] + ["tap", "down", "wait", "300"] * n +
                       ["tap", "cross", "wait", "500"])

    def revert(c):
        c.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])

    def room_change(world, room, label, progs=None):
        ms = {c.name: c.mark() for c in copies}
        if progs:
            bench.debug("A", 12, world, room, 0, 0, *progs, 0, fmt="<IiBBBBHHHH")
        else:
            bench.warp(world, room)
        bench.wait(f"both in {world:02X}/{room:02X}", lambda: all(c.room() == (world, room) for c in copies), 90, copies)
        time.sleep(8)
        check(label)
        for c in copies:
            held[c.name].clear()
            new = c.since(ms[c.name])
            drops = [l[13:170] for l in new if "file census: drop from group" in l]
            gone = [l[13:200] for l in new if "neg files: the drop from group" in l]
            bugs = [l[13:200] for l in new if "neg files: BUG" in l]
            left = ours(c)
            # The copy's own Keyblade, asked into ours again once the new room settled (option B), may be there.
            blade = any("copy files: the copy's Keyblade" in l and "files of ours" in l for l in new)
            if blade:
                left = {k: v for k, v in left.items() if not k[1].startswith("obj/W_")}
            out.append(f"  {c.name} {label}: drops {drops[:6]}; {gone or ['ours NOT let go by a drop']}; "
                       f"negative files now {len(left)}")
            if bugs or (state.get(c.name, {}).get("ours") and not gone):
                problems.append(f"{c.name} {label}: ours not let go at the room's drop ({bugs or 'no let-go line'})")
            if left:
                problems.append(f"{c.name} {label}: {len(left)} negative files left in the new room")
            state[c.name] = snap(c)

    keeper = DriveKeeper(copies)  # the infinite Drive gauge (the summons, the Limit, the Drives)
    if a.shared:
        out.append(f"The no-join rule: {[(c.name, no_join(c)) for c in copies]}")
        load_both(copies, a.slot, a.room)
        room_change(0x02, 0x28, "the warp to the gym")
        out.append("Ours first, then our own Drive (Valor's look, then each game's Sora into Valor):")
        for c in copies:
            ask(c, 1)
        all_loaded("Valor's look")
        baseline()
        placed_right("Valor's look")
        for c in copies:
            m = c.mark()
            drive(c, 0)
            time.sleep(5)
            check(f"{c.name}'s Drive into Valor with Valor's look ours")
            made = [l[13:200] for l in c.since(m) if "neg files: the game's" in l]
            out.append(f"  {c.name}: form {form_now(c)}; the game's requests given new entries: {len(made)}")
            out.extend(f"      {l}" for l in made[:10])
            verify(f"{c.name}'s Drive into Valor")
            placed_right(f"{c.name}'s Drive into Valor")
            revert(c)
            time.sleep(5)
            check(f"{c.name}'s Revert")
            verify(f"{c.name}'s Revert")
        out.append("Our own Drive first, then ours (each game's Sora into Wisdom, then Wisdom's look):")
        for c in copies:
            drive(c, 1)
            time.sleep(5)
            check(f"{c.name}'s Drive into Wisdom")
            ask(c, 2)
            all_loaded(f"{c.name}'s Wisdom look")
            verify(f"{c.name}: Wisdom's look asked while in Wisdom", new_ok=True)
            placed_right(f"{c.name}: Wisdom's look while in Wisdom")
            revert(c)
            time.sleep(5)
            check(f"{c.name}'s Revert from Wisdom")
            verify(f"{c.name}'s Revert from Wisdom")
            placed_right(f"{c.name}'s Revert from Wisdom")
        room_change(a.room[0], a.room[1], "the room change")
    elif a.scene:
        out.append("Walk-in scene (save 11, the Mysterious Tower 02/19):")
        load_both(copies, a.slot, a.room)
        room_change(0x02, 0x19, "the warp to the tower", progs=(2, 0, 1))
        for c in copies:
            ask(c, 1)
        all_loaded("Valor's look at the tower")
        baseline()
        box = walk_in_box(host)
        out.append(f"  host: walk-in area {box}")
        if not box:
            problems.append("no type-3 walk-in area found in the room")
        else:
            m = host.mark()
            bench.debug("A", 15, *box["centre"], fmt="<Iifff")
            started = None
            end = time.time() + 25
            while time.time() < end and not started:
                hits = [l for l in host.since(m) if "scene sync: " in l and "area" in l or "cutscene '" in l]
                started = hits[0] if hits else None
                time.sleep(0.3)
            out.append(f"  host: {started[13:200] if started else 'NO walk-in scene started'}")
            if not started:
                problems.append("the walk-in scene didn't start")
            for k in range(10):  # through the scene, every 2 s, until the room reloads
                time.sleep(2)
                check(f"the scene, {2 * (k + 1)} s")
                if not all(read_u(c, c.game.base + 0x7177B8, 1) for c in copies):
                    out.append(f"  the room reloads after {2 * (k + 1)} s")
                    break
                verify(f"the scene, {2 * (k + 1)} s")
            time.sleep(10)
            check("the scene's reload")
            for c in copies:
                new = c.since(marks[c.name])
                out.append(f"  {c.name}: {[l[13:170] for l in new if 'neg files: the drop' in l or 'neg files: BUG' in l or 'MOVED' in l][:6]}")
                if any("neg files: BUG" in l or ("neg files:" in l and "MOVED" in l) for l in new):
                    problems.append(f"{c.name}: a MOVED or BUG line through the scene")
    elif a.decline:
        out.append("Decline (KH2COOP_NEG_BUDGET_KB=100):")
        load_both(copies, a.slot, a.room)
        room_change(0x02, 0x28, "the warp to the gym")
        baseline()
        for c in copies:
            lines, result = negfiles(c, 1)
            out.append(f"  {c.name} negfiles 1: {result[13:300] if result else 'NO ANSWER'}")
            if "DECLINED" not in result:
                problems.append(f"{c.name}: no decline line")
        time.sleep(2)
        verify("the declined request")
        drive(host, 0)
        time.sleep(5)
        check("the host's Drive after the decline")
        for c in copies:
            left = [(n, g) for e, n, s, g, pl, sz, f in file_list(c) if g == 30000 or g < 0]
            out.append(f"  {c.name} after a layout: negative or 30000 entries {left[:8]}")
        verify("a Drive after the decline")
        revert(host)
        time.sleep(4)
    else:
        for c in copies:  # the Limit abilities before the gym's room load builds the ability bits (gym_limit)
            equip_limits(c)
        out.append("The gate (sent during the warp to the gym):")
        load_both(copies, a.slot, a.room)
        early = {c.name: c.mark() for c in copies}
        bench.warp(0x02, 0x28)
        time.sleep(0.6)
        for c in copies:
            bench.debug(c.name, 24, 1)
        bench.wait("both in the gym", lambda: all(c.room() == (0x02, 0x28) for c in copies), 90, copies)
        time.sleep(8)
        check("the gym")
        for c in copies:  # the spawn step doesn't run during the load: the request waits for it (information only)
            res = [l[13:200] for l in c.since(early[c.name]) if "neg files: form" in l]
            out.append(f"  {c.name} (sent 0.6 s into the warp): {res[-1:] or ['NO ANSWER']}")
            if res and "files of ours" in res[-1]:
                held[c.name].add(1)
        all_loaded("Valor's look (the early request)")
        baseline()
        out.append("The gate (Wisdom's look asked 50 ms after Final's, while Final's reads are in flight):")
        for c in copies:
            gm = c.mark()
            bench.debug(c.name, 24, 5)
            time.sleep(0.05)
            bench.debug(c.name, 24, 2)
            time.sleep(3)
            res = [l[13:200] for l in c.since(gm) if ("neg files: form" in l or "copy files: form" in l
                                                      or "DECLINED: form" in l)
                   and ("refused" in l or "files of ours" in l or "DECLINED" in l)]
            out.append(f"  {c.name}: {res or ['NO ANSWER']}")
            held[c.name].update(f for f, l in zip((5, 2), res) if "files of ours" in l)
            if len(res) != 2 or "refused" not in res[1]:
                problems.append(f"{c.name}: the request during our own reads wasn't refused ({res})")
        all_loaded("Final's look")
        verify("Final's look asked (Valor's must not move)", new_ok=True)
        out.append("Wisdom's look (the next groups up):")
        for c in copies:
            ask(c, 2)
        all_loaded("Wisdom's look")
        verify("Wisdom's look asked (Valor's must not move)", new_ok=True)
        out.append("The game's events:")
        for n in (2, 3):  # not 0, 1, 4: Valor's, Wisdom's and Final's looks are ours, and our own Drive into them
            drive(host, n)    # crashes (L149: its request joins our entries, its Keyblade build finds none in its group)
            time.sleep(5)
            check(f"host Drive entry {n}")
            verify(f"the host's Drive, entry {n} (form {form_now(host)})")
            revert(host)
            time.sleep(5)
            check(f"host Revert {n}")
            verify(f"the host's Revert {n}")
        drive(friend, 2)
        time.sleep(5)
        check("friend Drive")
        verify(f"the friend's Drive, entry 2 (form {form_now(friend)})")
        revert(friend)
        time.sleep(5)
        verify("the friend's Revert")
        host.game.pad.run(MENU_FIRST_WEAPON)
        time.sleep(4)
        check("the menu swap")
        verify("the host's pause-menu Keyblade swap", new_ok=True)  # the friend's copy of the host: its new Keyblade
        host.game.pad.run(PAGE2 + ["tap", "up", "wait", "500", "tap", "up", "wait", "500"])
        for step in range(4):
            time.sleep(2.5)
            host.game.pad.run(["tap", "cross", "wait", "300"])
        time.sleep(12)
        check("the summon")
        verify("the host's summon")
        host.game.pad.run(["tap", "down", "wait", "500", "tap", "cross", "wait", "500"])
        time.sleep(12)
        check("the dismissal")
        verify("the summon's dismissal")
        host.game.pad.run(PAGE2 + ["tap", "up", "wait", "500", "tap", "up", "wait", "500"])
        for step in range(4):
            time.sleep(2.5)
            host.game.pad.run(["tap", "cross", "wait", "300"])
        time.sleep(12)
        check("the second summon")
        verify("the host's second summon")
        host.game.pad.run(["tap", "down", "wait", "500", "tap", "cross", "wait", "500"])
        time.sleep(12)
        check("the second dismissal")
        verify("the second summon's dismissal")
        bench.debug("A", 19, 0x13E | 1 << 16)
        time.sleep(8)
        check("the Dusk")
        verify("a Dusk spawned (a new enemy type mid-room; invincible, an enemy for the Limit)")
        for c in copies:
            bench.debug(c.name, 20, 1)
        time.sleep(1.5)
        lm = host.mark()
        host.game.pad.run(PAGE2)
        for step in range(4):
            time.sleep(2.5)
            host.game.pad.run(["tap", "cross", "wait", "300"])
        time.sleep(20)
        check("the Limit")
        out.append(f"  host: Limit commands {len(find(host.since(lm), 'type 0x51)'))}")
        verify("the host's Limit")
        for c in copies:
            bench.debug(c.name, 20, 0)
        time.sleep(3)
        verify("the battle state off")
        out.append("The room change (back to 04/06):")
        room_change(a.room[0] if a.room else 0x04, a.room[1] if a.room else 0x06, "the room change")
        out.append("The other order (our Drive into Valor first, then Valor's look):")
        drive(host, 0)
        time.sleep(5)
        check("host Drive before the request")
        joins = ask(host, 1, expect_join=True)
        out.append(f"  host: {len(joins)} files joined the game's own entries")
        revert(host)
        time.sleep(5)
        check("host Revert after the shared request")
    for c in copies:
        new = c.since(marks[c.name])
        moved = [l[13:200] for l in new if "neg files: MOVED" in l or "neg files: the heap's end MOVED" in l
                 or "neg files: our bottom MOVED" in l or "neg files: BUG" in l]
        out.append(f"{c.name}: DLL move/BUG lines {len(moved)}: {moved[:8]}")
        out.append(f"{c.name}: census room summaries {[l[13:220] for l in new if 'file census: room' in l and 'left' in l][:8]}")
        if moved:
            problems.append(f"{c.name}: {len(moved)} DLL move/BUG lines")
        # The event block (5.2.6 step 4): each event's blocks served from ours, given back swept and balanced.
        served = [l[13:230] for l in new if "event block: the event's" in l and "served from ours" in l]
        given = [l[13:260] for l in new if "event block: our" in l and "given back (from" in l]
        bad = [l[13:260] for l in new if "BUG: event block" in l]
        bad += [l for l in given if "as at the take" not in l or " 0 of " not in l]
        out.append(f"{c.name}: event block: {len(served)} served, {len(given)} given back, {len(bad)} bad")
        for l in (served + given)[:8]:
            out.append(f"    {l}")
        if bad or len(served) != len(given):
            problems.append(f"{c.name}: event block: {len(served)} served, {len(given)} given back, bad {bad[:3]}")
        if c is host and not (a.shared or a.scene or a.decline) and not served:
            problems.append("host: no event block served from ours (the summons)")
    keeper.stop()
    out.append(f"Drive gauge keeper: {keeper.report()}")
    out.append(f"problems: {problems or 'none'}")
    return not problems and all(c.alive() and not c.crash for c in copies)


ENTRY_FORM_59 = {0: 1, 1: 2, 2: 3, 3: 4, 4: 5}  # save 59's Drive list: Valor, Wisdom, Limit, Master, Final


def walk_in_box(c):
    """The first type-3 spawn group (a walk-in story area) of the room with its first box: name, id and the box's
    centre in the world (the box test exe+0x421180 transforms the position by the matrix at box+8 and compares it to
    the half-sizes at +0x48, F5; the centre is where that transform gives 0; the convention is read from where the
    translation sits)."""
    base = c.game.base
    count = read_u(c, base + 0x2A10418, 4)
    for i in range(min(count, 64)):
        entry = base + 0x2A10010 + 16 * i
        group = read_u(c, entry + 8, 8)
        if not group:
            continue
        desc = read_u(c, group + 8, 8)
        if not desc or read_u(c, desc, 1) != 3:
            continue
        box = read_u(c, group + 0x10, 8)
        if not box:
            continue
        m = struct.unpack("<16f", peek.read(c.game.h, box + 8, 64))
        half = struct.unpack("<3f", peek.read(c.game.h, box + 0x48, 12))
        if abs(m[12]) + abs(m[13]) + abs(m[14]) >= abs(m[3]) + abs(m[7]) + abs(m[11]):  # local = p M (rows)
            r = [[m[0], m[1], m[2]], [m[4], m[5], m[6]], [m[8], m[9], m[10]]]
            t = [m[12], m[13], m[14]]
        else:  # local = M p (columns): the same as rows with M transposed
            r = [[m[0], m[4], m[8]], [m[1], m[5], m[9]], [m[2], m[6], m[10]]]
            t = [m[3], m[7], m[11]]
        det = (r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0])
               + r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]))
        if abs(det) < 1e-9:
            continue
        inv = [[(r[(j + 1) % 3][(i2 + 1) % 3] * r[(j + 2) % 3][(i2 + 2) % 3]
                 - r[(j + 1) % 3][(i2 + 2) % 3] * r[(j + 2) % 3][(i2 + 1) % 3]) / det for j in range(3)]
               for i2 in range(3)]
        centre = [-sum(t[k] * inv[k][j] for k in range(3)) for j in range(3)]
        name = peek.read(c.game.h, entry, 4).split(b"\x00")[0].decode("latin-1")
        return {"name": name, "id": read_u(c, desc + 2, 2), "centre": centre, "half": [round(v, 2) for v in half]}
    return None


def form_room(copies, a, out):
    """TODO 1.104 / 1.106 (two-PC run 2026-10-22: the friend in Wisdom, a room change, then its Sora stuck when
    jumping; the copy's Drive shine back after the room change). Save 59 (04/06): --by (B = the friend) Drives into
    the --pick'th entry (1 = Wisdom: 0 Valor, 1 Wisdom, ...; run with --by B --pick 1), then both warp to 04/09, so both games load
    the room with that form on. The driver then walks, jumps 3 times and walks again; its Sora's spot (actor+0x670),
    height and motion are logged. The other game's copy: its effect listings after the warp (form_look.cpp's 1.5 s
    check) and screenshots. PASS = no crash, the driver still in the form after the warp, it moved and every jump went
    up, and the other game's last listing shows no 0x200 effect still alive (none left or all fading out). Run with
    --env KH2COOP_SORA_SHEET_FIX=0 to see whether our stat-sheet fix is behind the stuck Sora."""
    host, friend = copies
    driver, seer = (friend, host) if a.by == "B" else (host, friend)
    pick = a.pick
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    form_now = lambda c: read_u(c, c.game.base + 0x9ACDD4, 1)
    sheet = read_u(driver, read_u(driver, driver.game.base + 0x2A171C8, 8) + 0x5C0, 8)
    write_mem(driver, sheet + 0x1B0, bytes([100, read_u(driver, sheet + 0x1B2, 1)]))  # a full gauge
    driver.game.pad.run(DRIVE + ["tap", "cross", "wait", "800"] + ["tap", "down", "wait", "300"] * pick +
                        ["tap", "cross", "wait", "500"])
    time.sleep(6)
    form = form_now(driver)
    out.append(f"{driver.name} Drove into entry {pick}: form {form}, form / link {sheet_link(driver)}")
    if not form:
        return False
    m2 = {c.name: c.mark() for c in copies}
    bench.warp(0x04, 0x09)
    bench.wait("both in 04/09", lambda: all(c.room() == (0x04, 0x09) for c in copies), 90, copies)
    time.sleep(4)
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"form_room_after_{c.name}.png"))
    sora = head(driver)
    spot = lambda: struct.unpack("<3f", peek.read(driver.game.h, sora + 0x670, 12))
    out.append(f"after the warp: {driver.name} form {form_now(driver)}, form / link {sheet_link(driver)}, motion "
               f"{bench.motion(driver, sora)}")
    moves, jumps = [], []
    for n in range(2):
        p0 = spot()
        driver.game.pad.run(["stick", "0", "1", "1200", "wait", "400"])
        p1 = spot()
        moves.append(round(((p1[0] - p0[0]) ** 2 + (p1[2] - p0[2]) ** 2) ** 0.5, 1))
        if n == 0:
            for _ in range(3):
                jumps.append(jump_test(driver, sora))
                out.append(f"  jump (start y, highest change, end change) {jumps[-1]}, motion {bench.motion(driver, sora)}")
                bench.screenshot(driver.hwnd, os.path.join(OUT, f"form_room_jump{len(jumps)}_{driver.name}.png"))
    out.append(f"{driver.name} walked {moves} (before / after the jumps), motion {bench.motion(driver, sora)}")
    for c in copies:
        c.lines()
        bench.screenshot(c.hwnd, os.path.join(OUT, f"form_room_end_{c.name}.png"))
        if c.crash or not c.alive():
            raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
    for c in copies:
        lines = [l[13:200] for l in c.since(marks[c.name]) if "form look" in l or "stat sheet" in l or
                 "Sora sheet" in l or "CRASH" in l]
        out.append(f"{c.name} form look / sheet lines ({len(lines)}):")
        out.extend(f"    {l}" for l in lines[:40])
    listings = [l for l in seer.since(m2[seer.name]) if "the copy's effects:" in l]
    left = 0
    if listings:  # "... N of the kind that ends with the old body, M fading out"
        import re
        mm = re.search(r"(\d+) of the kind that ends with the old body, (\d+) fading out", listings[-1])
        left = int(mm.group(1)) - int(mm.group(2)) if mm else 0
    out.append(f"{seer.name}: effect listings after the warp {len(listings)}, last: "
               f"{listings[-1][13:160] if listings else '-'}; still on {left}")
    went_up = all(j[1] > 0.5 or j[1] < -0.5 for j in jumps)  # KH2's y points down: a jump is a big change either way
    return (all(c.alive() and not c.crash for c in copies) and form_now(driver) == form and all(moves) and went_up
            and left == 0)


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


def file_list(c):
    """The game's file list (exe+0x29F33C8, next = decode(+0x70)): [(entry, name, state +1, group +0x48, place +0x58,
    size +0x50, flags +0x44)]."""
    h, base = c.game.h, c.game.base
    q = lambda addr: struct.unpack("<Q", peek.read(h, addr, 8))[0]
    out, r, seen = [], q(base + 0x29F33C8), set()
    while r and r not in seen and len(out) < 4000:
        seen.add(r)
        raw = peek.read(h, r, 0x78)
        name = raw[4:0x44].split(b"\x00")[0].decode("latin-1")
        flags, group = struct.unpack_from("<Ii", raw, 0x44)
        size, place = struct.unpack_from("<QQ", raw, 0x50)
        out.append((r, name, raw[1], group, place, size, flags))
        r = bench.decode(c, struct.unpack_from("<I", raw, 0x70)[0])
    return out


def blade_files(c, w, files):
    """A weapon actor's object, group (+0xC0C) and the list entries its set (+0x920/+0x928/+0x930) and motion
    controller (+0x158) point into."""
    h = c.game.h
    q = lambda addr: struct.unpack("<Q", peek.read(h, addr, 8))[0]
    i32 = lambda addr: struct.unpack("<i", peek.read(h, addr, 4))[0]
    parts = [f"weapon exe+0x{w - c.game.base:X} {bench.model_of(c, w)} object {i32(w + 0x914):#x} group {i32(w + 0xC0C)}"]
    for off in (0x920, 0x928, 0x930, 0x158):
        p = q(w + off)
        hit = next(((r, n, s, g) for r, n, s, g, pl, sz, f in files if pl and pl <= p < pl + max(sz, 1)), None)
        parts.append(f"+{off:#x} -> " + (f"{hit[1]} (entry exe+0x{hit[0] - c.game.base:X}, group {hit[3]}, state {hit[2]})"
                                         if hit else ("-" if not p else f"{p:#x} (no entry)")))
    return parts


def keyblade(copies, a, out):
    """TODO 3.3 / 1.110 research (read only): in each game, the Keyblade items in the save (Sora's record +0/+2 at
    exe+0x9ABDA0, the 10 form records' first u16 at +0xE04 + 0x38 n), our Sora's and the copy's weapon actors with
    their object, group and the file entries they use, and every listed weapon file (W_*) with its group."""
    load_both(copies, a.slot, a.room)
    time.sleep(3)
    for c in copies:
        h, base = c.game.h, c.game.base
        q = lambda addr: struct.unpack("<Q", peek.read(h, addr, 8))[0]
        u16 = lambda addr: struct.unpack("<H", peek.read(h, addr, 2))[0]
        rec = base + 0x9ABDA0
        out.append(f"{c.name}: Sora's record weapons {u16(rec):#x} {u16(rec + 2):#x}; form records' items "
                   + " ".join(f"{u16(rec + 0xE04 + 0x38 * n):#x}" for n in range(10)))
        files = file_list(c)
        sora = q(base + 0x2A171C8)
        copy = next((x for x, n in bench.actors(c) if n.startswith("P_EX100") and x != sora), 0)
        for who, x in (("our Sora", sora), ("Sora copy", copy)):
            if not x:
                out.append(f"   {who}: none")
                continue
            rec_h = struct.unpack("<I", peek.read(h, x + 8, 4))[0]
            entry = bench.decode(c, rec_h)
            kind = u16(entry + 0x4E) if entry else -1
            out.append(f"   {who} exe+0x{x - base:X} {bench.model_of(c, x)}: kind {kind:#x}, form byte +0x57 "
                       f"{peek.read(h, entry + 0x57, 1)[0] if entry else -1}")
            for k, off in enumerate((0xD60, 0xD68)):
                w = q(x + off)
                if w:
                    for line in blade_files(c, w, files):
                        out.append(f"      [{k}] {line}")
        out.append(f"   weapon files in the list ({len(files)} entries):")
        for r, n, s, g, pl, sz, f in files:
            if "/W_" in n or n.startswith("W_"):
                out.append(f"      exe+0x{r - base:X} {n} group {g} state {s} flags {f:#x} place {pl:#x} size {sz:#x}")
    return all(c.alive() and not c.crash for c in copies)


def saferead(copies, a, out):
    """docs/CRASH_RISK_REVIEW.md section 4 (safe_read.cpp, crash_log.cpp): both games in a room, then the saferead
    test command in each (100 faulting reads landed as false, our guard page and another thread's stack guard page
    put back, that thread's stack growing past it); both games must still run 20 s later. Also prints the start-up
    lines of the crash logger's measurements (clr.dll, the frame thread, other handlers added)."""
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    for c in copies:
        bench.debug(c.name, 23, 0)
        time.sleep(3)
    time.sleep(20)
    ok = True
    for c in copies:
        start = [l for l in c.all[:marks[c.name]] if "crash logger" in l or "safe read:" in l][:12]
        new = c.since(marks[c.name])
        result = [l for l in new if "saferead test" in l and "expected" not in l]
        bugs = find(new, "BUG: safe read")
        good = c.alive() and not c.crash and bool(find(new, "saferead test: PASS")) and not bugs
        ok &= good
        out.append(f"{c.name}: alive {c.alive()}, crash {c.crash or '-'}, unexpected guard hits {len(bugs)}")
        out += [f"   {l}" for l in start + result + find(new, "safe read:") + find(new, "fault in our DLL")]
    return ok


def equip_menu(copies, a, out):
    """Research for the blade bench: the host opens the pause menu and presses through it (cross, then the --keys
    list), a screenshot after each press (equip_<n>.png)."""
    host = copies[0]
    load_both(copies, a.slot, a.room)
    keys = ["start"] + (a.keys.split(",") if a.keys else ["cross", "cross", "cross"])
    for n, k in enumerate(keys):
        host.game.pad.run(["tap", k, "wait", "1200"])
        time.sleep(0.5)
        bench.screenshot(host.hwnd, os.path.join(OUT, f"equip_{n}.png"))
        out.append(f"{n}: {k}")
    return all(c.alive() and not c.crash for c in copies)


SORA_RECORD = 0x9ABDA0  # Sora's character record: u16 +0 the Keyblade item (VERIFIED_OFFSETS L121, F332)


def copy_blade_state(c, label, out):
    """Our Sora's and the copy's Keyblades in one game: model, group, the files they use."""
    h, base = c.game.h, c.game.base
    q = lambda addr: struct.unpack("<Q", peek.read(h, addr, 8))[0]
    files = file_list(c)
    sora = q(base + 0x2A171C8)
    copy = next((x for x, n in bench.actors(c) if n.startswith("P_EX100") and x != sora), 0)
    models = {}
    out.append(f"   {c.name} {label}: record item {read_u(c, base + SORA_RECORD, 2):#x}")
    for who, x in (("our Sora", sora), ("copy", copy)):
        w = q(x + 0xD60) if x else 0
        models[who] = bench.model_of(c, w) if w else None
        if not w:
            out.append(f"      {who}: no Keyblade")
            continue
        parts = blade_files(c, w, files)
        out.append(f"      {who}: {parts[0]}")
        out.extend(f"         {p}" for p in parts[1:])
    return models


MENU_FIRST_WEAPON = (["tap", "start", "wait", "1500"] + ["tap", "cross", "wait", "1000"] * 3 +
                     ["tap", "cross", "wait", "1500"] + ["tap", "circle", "wait", "800"] * 4)  # Items, Sora, Weapons,
# the first Keyblade in the list (save 59: Kingdom Key while Ultima Weapon is equipped), then back out of the menu


def blade(copies, a, out):
    """TODO 3.3 / 1.110 (copy_blade.cpp), every change through the real pause menu (Items, Sora, Weapons, the first
    in the list, then out): (1) the friend changes Keyblade: the host's copy of the friend takes it (its files in group
    5099); (2) the host changes to the same one (both carry it; the friend's copy of the host shares the friend's own
    files); (3) the host changes again: the game frees the old Keyblade's files at once, which the host's copy of the
    friend shares: no crash, the copy keeps a Keyblade; (4) the friend's form Keyblades (Valor's and Final's records
    written: the game builds a form's second Keyblade from its own new files) on the host's copy during the friend's
    Drives. The other player attacks after each change. (Writing the main Keyblade into the record without the menu
    made the game's own Drive crash, exe+0x39CC40: the form body takes the record's Keyblade from our Sora's files.)
    --forms-only: only step 4. Run with --env KH2COOP_COPY_BLADE=0 for the old behaviour (1.110: step 3 crashed)."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    ok = True

    def check(label):
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'} ({label}): {c.crash or ''}")

    def attack(c):
        c.game.pad.run(["tap", "cross", "wait", "350"] * 4)

    def shots(step):
        for c in copies:
            bench.screenshot(c.hwnd, os.path.join(OUT, f"blade_{step}_{c.name}.png"))

    def menu_change(c, label):
        c.game.pad.run(MENU_FIRST_WEAPON)
        time.sleep(3)
        check(f"{label}, after the menu")
        out.append(f"   {c.name}'s item now {read_u(c, c.game.base + SORA_RECORD, 2):#x}, menu byte "
                   f"{read_u(c, c.game.base + 0x7435D0, 1):#x}")
        other = friend if c is host else host
        attack(other)
        attack(c)
        time.sleep(1)
        check(label)
        shots(label)

    if a.forms_only:
        return blade_forms(copies, host, friend, marks, out, check, attack, shots) and ok
    out.append("start:")
    before = {c.name: copy_blade_state(c, "start", out) for c in copies}
    out.append("step 1: the friend changes Keyblade in the pause menu")
    menu_change(friend, "1")
    m = copy_blade_state(host, "after step 1", out)
    if m["copy"] == before["A"]["copy"]:
        out.append("   FAIL: the host's copy of the friend kept its Keyblade")
        ok = False
    out.append("step 2: the host changes Keyblade in the pause menu (to the friend's)")
    menu_change(host, "2")
    for c in copies:
        if not copy_blade_state(c, "after step 2", out)["copy"]:
            out.append(f"   FAIL: {c.name}'s copy has no Keyblade")
            ok = False
    out.append("step 3: the host changes Keyblade again (its old files, shared by its copy of the friend, freed)")
    menu_change(host, "3")
    for c in copies:
        if not copy_blade_state(c, "after step 3", out)["copy"]:
            out.append(f"   FAIL: {c.name}'s copy has no Keyblade")
            ok = False
    return blade_forms(copies, host, friend, marks, out, check, attack, shots) and ok


def blade_swap(copies, a, out):
    """TODO 3.3: your crash of 2026-10-25 21:59 (friend kh2coop_22044). The friend changes Keyblade in the menu (the
    friend's copy of the host then keeps the host's Ultima Weapon in its own files, group 5099); the host changes to
    one the friend doesn't carry (the second in the list) and runs off the moment its menu closes, so the friend's game
    swaps a visible, moving copy's Keyblade from files of ours to new files of ours. Then both attack. Before the
    host's change only the friend's game goes into the forced battle state (debug battle on; in battle Start opens
    the battle menu, no Keyblades), so its copy has the Keyblade out, as in your fight. --pick <n>: the host's pick is
    the n-th in the list (default 2)."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    ok = True

    def check(label):
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'} ({label}): {c.crash or ''}")

    def pick(n, last_wait):
        return (["tap", "start", "wait", "1500"] + ["tap", "cross", "wait", "1000"] * 3 +
                ["tap", "down", "wait", "400"] * (n - 1) + ["tap", "cross", "wait", "1500"] +
                ["tap", "circle", "wait", "800"] * 3 + ["tap", "circle", "wait", str(last_wait)])

    out.append("start:")
    copy_blade_state(friend, "start", out)
    out.append("step 1: the friend changes Keyblade in the pause menu (the first in its list)")
    friend.game.pad.run(pick(1, 800))
    time.sleep(3)
    check("step 1")
    out.append(f"   friend's item now {read_u(friend, friend.game.base + SORA_RECORD, 2):#x}")
    copy_blade_state(friend, "after step 1", out)
    if a.drive:  # as in your run: the friend's own Valor and Revert first (the copy's Keyblade files move)
        out.append("step 1b: the friend Drives into Valor, attacks, Reverts")
        sheet = read_u(friend, read_u(friend, friend.game.base + 0x2A171C8, 8) + 0x5C0, 8)
        write_mem(friend, sheet + 0x1B0, bytes([100, read_u(friend, sheet + 0x1B2, 1)]))
        friend.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])
        time.sleep(6)
        friend.game.pad.run(["tap", "cross", "wait", "350"] * 6)
        check("step 1b, Valor")
        friend.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])  # Revert
        time.sleep(5)
        check("step 1b, after the Revert")
    bench.debug(friend.name, 20, 1)
    time.sleep(2)
    out.append(f"battle state (host, friend): {[battle_state(c) for c in copies]}")
    n = a.pick or 2

    def copy_flags():
        sora = read_u(friend, friend.game.base + 0x2A171C8, 8)
        copy = next((x for x, nm in bench.actors(friend) if nm.startswith("P_EX100") and x != sora), 0)
        w = read_u(friend, copy + 0xD60, 8) if copy else 0
        return f"{read_u(friend, w + 0x124, 4):#x}" if w else "no Keyblade"

    out.append(f"   the friend's copy's Keyblade flags +0x124 (0x400 away, 0x700 hidden): {copy_flags()}")
    out.append("step 2: the host attacks (its copy draws the Keyblade), changes to the "
               f"{n}-th in its list at once and runs off as the menu closes")
    host.game.pad.run(["tap", "cross", "wait", "350"] * 4)
    out.append(f"   after the attacks: {copy_flags()}")
    host.game.pad.run(pick(n, 0))
    host.game.run(90, 1500)
    host.game.run(270, 1500)
    time.sleep(1)
    check("step 2")
    item = read_u(host, host.game.base + SORA_RECORD, 2)
    out.append(f"   host's item now {item:#x}, friend's {read_u(friend, friend.game.base + SORA_RECORD, 2):#x}")
    m = copy_blade_state(friend, "after step 2", out)
    if not m["copy"]:
        out.append("   FAIL: the friend's copy has no Keyblade")
        ok = False
    for c in copies:
        c.game.pad.run(["tap", "cross", "wait", "350"] * 6)
    time.sleep(2)
    check("attacks")
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"blade_swap_{c.name}.png"))
        lines = [l[13:220] for l in c.since(marks[c.name]) if "copy blade" in l or "CRASH" in l]
        out.append(f"{c.name} lines ({len(lines)}):")
        out.extend(f"    {l}" for l in lines[:30])
    return ok and all(c.alive() and not c.crash for c in copies)

def blade_forms(copies, host, friend, marks, out, check, attack, shots):
    """The blade bench's step 4: the friend's own form Keyblades (Valor's and Final's records) on the host's copy."""
    ok = True
    forms = ((0, 1, 0x2B, "Valor"), (4, 5, 0x29, "Final"))  # Drive list entry, form record, item
    for _, rec, item, _ in forms:
        write_mem(friend, friend.game.base + SORA_RECORD + 0xE04 + 0x38 * (rec - 1), struct.pack("<H", item))
    time.sleep(3)
    for entry, rec, item, name in forms:
        out.append(f"step 4: the friend Drives into {name} (its {name} Keyblade set to {item:#x})")
        sheet = read_u(friend, read_u(friend, friend.game.base + 0x2A171C8, 8) + 0x5C0, 8)
        write_mem(friend, sheet + 0x1B0, bytes([100, read_u(friend, sheet + 0x1B2, 1)]))
        friend.game.pad.run(DRIVE + ["tap", "cross", "wait", "800"] + ["tap", "down", "wait", "300"] * entry +
                            ["tap", "cross", "wait", "500"])
        time.sleep(6)
        attack(friend)
        time.sleep(1)
        check(f"step 4, {name}")
        shots(f"4{name}")
        h, base = host.game.h, host.game.base
        sora = read_u(host, base + 0x2A171C8, 8)
        copy = next((x for x, n in bench.actors(host) if n.startswith("P_EX100") and x != sora), 0)
        w1 = read_u(host, copy + 0xD68, 8) if copy else 0
        out.append(f"   host's copy: body {bench.model_of(host, copy) if copy else '-'}, second Keyblade "
                   f"{bench.model_of(host, w1) if w1 else 'none'}")
        if not w1:
            out.append(f"   FAIL: no second Keyblade on the host's copy in {name}")
            ok = False
        friend.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])  # Revert
        time.sleep(5)
        check(f"step 4, after {name}'s Revert")
    for c in copies:
        lines = [l[13:220] for l in c.since(marks[c.name])
                 if "copy blade" in l or "second Keyblade" in l or "CRASH" in l]
        out.append(f"{c.name} lines ({len(lines)}):")
        out.extend(f"    {l}" for l in lines[:50])
    return ok and all(c.alive() and not c.crash for c in copies)


SCENARIOS = {"ship": ship, "alone": alone, "downed": downed, "forms": forms, "limit": limit, "gym_limit": gym_limit, "summon": summon, "party": party, "companions": companions, "companion_ko": companion_ko, "mp_probe": mp_probe, "xemnas": xemnas, "xend": xend, "xlimit": xlimit, "xcontinue": xcontinue, "menu": menu, "hud": hud, "hud2": hud2, "item": item, "costume": costume, "inspect": inspect, "heal": heal, "mickey": mickey, "mickey_coop": mickey_coop, "getup": getup, "mickey_real": mickey_real, "swarm": swarm, "valor_sheet": valor_sheet, "formlook": formlook, "form_room": form_room, "keyblade": keyblade, "blade": blade, "blade_swap": blade_swap, "equip_menu": equip_menu, "saferead": saferead, "form_share": form_share, "file_groups": file_groups, "neg_group": neg_group, "limit_gap": limit_gap}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scenario", choices=sorted(SCENARIOS))
    ap.add_argument("--slot", type=int, default=52)  # HB Borough 04/06, Donald + Goofy
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--drive", action="store_true")  # blade_swap: the friend's Valor + Revert first
    ap.add_argument("--forms-only", action="store_true")  # blade: only step 4 (the form Keyblades)
    ap.add_argument("--keys", default="")  # equip_menu: comma-separated pad taps after start
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
    ap.add_argument("--program", type=int, default=74)  # xemnas/xend/...: 74 story Final Xemnas, 98 Data Xemnas (part 2)
    ap.add_argument("--seconds", type=int, default=420)  # xend: give up after this long
    ap.add_argument("--fx-census", action="store_true")  # xemnas: live effects and their particles every 1 s
    ap.add_argument("--winner", choices=("A", "B"), default="A")  # mickey_coop: who wins the roll (test setting)
    ap.add_argument("--end", choices=("rescue", "over", "dies", "nobody"), default="rescue")  # mickey_coop: how it ends
    ap.add_argument("--dump", action="store_true")  # mickey_coop: both Soras' actor / movement bytes before and after
    ap.add_argument("--trace", action="store_true")  # mickey: fades and Game Over flags every 50 ms (mickey_trace_<copy>.txt)
    ap.add_argument("--friend-back", action="store_true")  # mickey: the friend's game takes the Mickey branch, no Mickey
    ap.add_argument("--one", action="store_true")  # formlook: only one player Drives (--by), files on demand
    ap.add_argument("--auto", action="store_true")
    ap.add_argument("--decline", action="store_true")  # neg_group: a budget too small (the decline path)
    ap.add_argument("--scene", action="store_true")  # neg_group: through the Pete walk-in scene (save 11, 02/19)
    ap.add_argument("--shared", action="store_true")  # neg_group: the no-join rule, the shared form in both orders
    ap.add_argument("--mixed", action="store_true")  # formlook: the friend Drives into the next entry  # formlook: no test commands, the copy follows the pose
    ap.add_argument("--low", type=int, default=5)  # heal: the HP a Sora is held at (a quarter of max or less)
    a = ap.parse_args()
    saves = {}
    with open(os.path.join(bench.ROOT, "build", "bench", "coop_saves.txt")) as fh:
        for line in fh:
            s, w, r = line.split()
            saves[int(s)] = (int(w, 16), int(r, 16))
    if a.scenario in ("formlook", "form_room", "form_share", "file_groups", "neg_group", "limit_gap") and a.slot == 52:
        a.slot = 59  # all Drive Forms but Anti
    if a.scenario == "neg_group" and a.scene:
        a.slot = 11  # then the warp to the Mysterious Tower's walk-in (the Pete scene)
    if a.scenario == "gym_limit" and a.slot == 52:
        a.slot = 59  # high level, Donald + Goofy (Trinity)
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
        if a.scenario in ("mickey_coop", "mickey_real") else []) + (
        ["KH2COOP_NEG_BUDGET_KB=100"] if a.scenario == "neg_group" and a.decline else []) + (
        ["KH2COOP_EVENT_BLOCK_GUARD=1", "KH2COOP_ENEMY_GOD=1"] if a.scenario == "neg_group" else []) + a.env
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
