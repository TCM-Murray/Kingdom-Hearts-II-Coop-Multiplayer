"""Hit comparison (TODO 1.79): does the friend's own game see the hits the host's game lands on the friend?

Usage: py tools/hit_compare.py <host log> <friend log> [--window-ms LO,HI]

Both logs need the 'hit compare:' lines (puppet.cpp LogHitCompare, on unless KH2COOP_HIT_COMPARE=0). The host's
lines are hits on the copy (what the friend takes today), the friend's are hits its own game applied to its own Sora
(thrown away today). A host hit is matched with the friend's first unmatched hit by the same attacker model from LO
to HI ms after it (default -300,900: the friend's game shows the host's enemies ~100 ms + network later). The two
logs must share a clock (a bench pair on one PC; on two PCs give --shift-ms, the friend's clock minus the host's).

Prints: matched hits and their delay, hits only in the host's game and only in the friend's (by attacker and hit
kind), and how often the friend's Sora was in a hit reaction (motion 10-39) 8 frames after its own hit.
"""
import argparse
import collections
import re
import statistics

HIT = re.compile(r"^(\d\d):(\d\d):(\d\d)\.(\d{3}) hit compare: on (our Sora|the copy): (-?\d+) raw, stat (\d+), "
                 r"kind (\d+), reaction (\d+), by (\S+) \(exe\+0x[0-9A-F]+\), motion (-?\d+)")
AFTER = re.compile(r"^(\d\d):(\d\d):(\d\d)\.(\d{3}) hit compare: our Sora's motion 8 frames after its own hit: (-?\d+)")


def ms(m):
    h, mi, s, f = (int(m.group(i)) for i in range(1, 5))
    return ((h * 60 + mi) * 60 + s) * 1000 + f


def read(path, who, shift=0):
    hits, afters = [], []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = HIT.match(line)
            if m and m.group(5) == who:
                hits.append({"t": ms(m) - shift, "dmg": int(m.group(6)), "kind": int(m.group(8)),
                             "react": int(m.group(9)), "by": m.group(10), "motion": int(m.group(11))})
                continue
            m = AFTER.match(line)
            if m:
                afters.append(int(m.group(5)))
    return hits, afters


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host")
    ap.add_argument("friend")
    ap.add_argument("--window-ms", default="-300,900")
    ap.add_argument("--shift-ms", type=int, default=0)
    a = ap.parse_args()
    lo, hi = (int(v) for v in a.window_ms.split(","))
    host, _ = read(a.host, "the copy")
    friend, afters = read(a.friend, "our Sora", a.shift_ms)
    used = [False] * len(friend)
    delays, host_only = [], []
    dmg_same = 0
    for h in host:
        best = None
        for i, f in enumerate(friend):
            if used[i] or f["by"] != h["by"]:
                continue
            d = f["t"] - h["t"]
            if lo <= d <= hi:
                best = i
                break
        if best is None:
            host_only.append(h)
            continue
        used[best] = True
        delays.append(friend[best]["t"] - h["t"])
        dmg_same += friend[best]["dmg"] == h["dmg"]
    friend_only = [f for i, f in enumerate(friend) if not used[i]]
    print(f"host hits on the copy {len(host)}, friend's own hits on its Sora {len(friend)}")
    print(f"matched {len(delays)} ({100 * len(delays) // max(1, len(host))}% of the host's), same damage {dmg_same}")
    if delays:
        print(f"  friend's hit after the host's: median {statistics.median(delays):.0f} ms, "
              f"min {min(delays)}, max {max(delays)}")
    for name, rows in (("only in the host's game", host_only), ("only in the friend's game", friend_only)):
        c = collections.Counter((r["by"], r["kind"]) for r in rows)
        print(f"{name}: {len(rows)}" + "".join(f"\n  {by} kind {k}: {n}" for (by, k), n in c.most_common()))
    react = sum(10 <= m < 40 for m in afters)
    print(f"friend's Sora 8 frames after its own hit: in a hit reaction (motion 10-39) {react} of {len(afters)}; "
          f"motions {collections.Counter(afters).most_common(8)}")


if __name__ == "__main__":
    main()
