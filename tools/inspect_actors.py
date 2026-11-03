"""Actor inspector (TODO 2.4): every actor in both copies' rooms, side by side, the same one on one row.

Usage: py tools/inspect_actors.py [--ports 27701,27702] [--watch <key>] [--every 0.5]
  no --watch: one table of both games' actor lists (asked through tools/kh2cmd.py, TODO 2.1)
  --watch <key>: that row again every --every seconds; key = a row's first column (net 12/3, sora:host,
                 sora:friend), a model name (its first actor) or an address (0x...)
Rows are paired by the mod's own tags: 'net N/I' is the host's number for an enemy (in the friend's game, the host
enemy it's bound to); sora:host / sora:friend is that player's Sora in its own game and its copy in the other one.
Actors with no tag (companions, chests, weapons) follow, paired as the nth of their object id in each list: a guess,
so their key starts with '~' (~obj 140.2). Both copies must run on this PC with KH2COOP_DEBUG=1 (the bench does).
"""
import argparse
import re
import sys
import time

sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
from kh2cmd import query  # noqa: E402

LINE = re.compile(r"^(?P<model>\S+?)#[0-9A-F]+ obj 0x(?P<obj>[0-9A-F]+) t(?P<type>\d+) team (?P<team>\d+) hp (?P<hp>-?\d+) "
                  r"pos \((?P<pos>[^)]*)\) mot (?P<mot>-?\d+)(?P<bank> \(bank\))?.*"
                  r"\|\| addr (?P<addr>0x[0-9A-F]+) max (?P<max>-?\d+) net (?P<net>\d+/\d+) who (?P<who>\S+)$")


def parse(line):
    m = LINE.match(line)
    return m.groupdict() if m else None


def key(a, role):
    other = "friend" if role == "host" else "host"
    if a["who"] in ("sora", "copy"):
        return f"sora:{role if a['who'] == 'sora' else other}"
    return f"net {a['net']}" if a["net"] != "0/0" else None


def cell(a):
    if not a:
        return ""
    mot = a["mot"] + ("b" if a["bank"] else "")
    return f"{a['model'][:14]:<14} {a['obj']:>4} t{a['team']} {a['hp']:>5}/{a['max']:<5} m{mot:<5} ({a['pos']})"


def snapshot(ports):
    """{role: [actor dict]} for each copy that answered; role from its ping (host / friend)."""
    out = {}
    for port in ports:
        pong = query(port, "ping")
        lines = query(port, "actors", timeout=2.0) if pong else None
        if not lines:
            print(f"port {port}: no answer (not running, no KH2COOP_DEBUG=1, or co-op off)")
            continue
        role = pong[0].split()[1]
        out[role] = [a for a in map(parse, lines) if a]
        print(f"port {port}: {role}, {pong[0].split('room ')[1]}, {len(out[role])} actors")
    return out


def rows(snap):
    """[(key, host actor or None, friend actor or None)]: paired rows first, then each game's unpaired ones."""
    paired, seen = {}, {}
    for role in ("host", "friend"):
        for a in snap.get(role, []):
            k = key(a, role)
            if not k:  # no mod tag (companions, chests, weapons): the nth of that object in each list, a guess: '~'
                n = seen[(role, a["obj"])] = seen.get((role, a["obj"]), 0) + 1
                k = f"~obj {a['obj']}.{n}"
            paired.setdefault(k, {})[role] = a
    def order(k):  # the Soras, then net ids in number order, then the guesses by object id
        if k.startswith("sora"):
            return 0, k, 0
        if k.startswith("net "):
            return (1, *map(int, k[4:].split("/")))
        obj, n = k[5:].split(".")
        return 2, int(obj, 16), int(n)
    return [(k, paired[k].get("host"), paired[k].get("friend")) for k in sorted(paired, key=order)]


def pick(snap, want):
    for k, h, f in rows(snap):
        if want in (k, *(a["addr"] for a in (h, f) if a)) or any(a and a["model"] == want for a in (h, f)):
            return k, h, f
    return None


def selftest():
    line = ("{}#1 obj 0x{} t4 team 2 hp 30 pos (1 2 3) mot 0 thr 1 f120 0x0 9b8 0x0 fade 1.00 cls exe+0x5 self4 1 | x "
            "|| addr {} max 30 net {} who {}").format
    host = [parse(line(*x)) for x in (("P_EX100", "236", "0x1", "0/0", "sora"), ("M_EX020", "12E", "0x2", "12/3", "-"),
                                      ("F_EX030", "140", "0x3", "0/0", "-"), ("F_EX030", "140", "0x4", "0/0", "-"))]
    friend = [parse(line(*x)) for x in (("P_EX100", "236", "0xA", "0/0", "copy"), ("M_EX020", "12E", "0xB", "12/3", "-"),
                                        ("F_EX030", "140", "0xC", "0/0", "-"), ("W_EX010", "67", "0xF", "0/0", "-"))]
    r = rows({"host": host, "friend": friend})
    assert [k for k, *_ in r] == ["sora:host", "net 12/3", "~obj 67.1", "~obj 140.1", "~obj 140.2"], r
    assert r[1][2]["addr"] == "0xB" and r[4][2] is None and pick({"host": host}, "0x4")[0] == "~obj 140.2"
    print("selftest ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ports", default="27701,27702")
    ap.add_argument("--watch")
    ap.add_argument("--every", type=float, default=0.5)
    ap.add_argument("--selftest", action="store_true")  # the parser and the pairing on made-up lines, no game
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    ports = [int(p) for p in a.ports.split(",")]
    if not a.watch:
        snap = snapshot(ports)
        print(f"{'key':<16} {'host':<62} | friend")
        for k, h, f in rows(snap):
            print(f"{k:<16} {cell(h):<62} | {cell(f)}")
        return
    while True:
        snap = {}
        for port in ports:  # quiet: no per-port lines while watching
            pong, lines = query(port, "ping"), query(port, "actors", timeout=2.0)
            if pong and lines:
                snap[pong[0].split()[1]] = [x for x in map(parse, lines) if x]
        hit = pick(snap, a.watch)
        stamp = time.strftime("%H:%M:%S")
        print(f"{stamp} {hit[0]:<16} {cell(hit[1]):<62} | {cell(hit[2])}" if hit else f"{stamp} {a.watch}: not there",
              flush=True)  # piped (a bench, a panel): each line as it comes
        time.sleep(a.every)


if __name__ == "__main__":
    main()
