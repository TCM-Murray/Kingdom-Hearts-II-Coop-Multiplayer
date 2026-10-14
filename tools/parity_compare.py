"""Compare what the host's and the friend's games showed of each enemy (TODO 1.55, one brain; TODO 1.2).

Both games must run with KH2COOP_PARITY=1 (src/dll/world_sync.cpp, 'Parity sampler'): every 250 ms of the host's
clock each one logs a 'parity:' line with every enemy's state. This script reads the two logs afterwards.

Usage: py tools/parity_compare.py <host log> <friend log> [--min-ms 500] [--pos-tol 60] [--type MODEL ...]
                                  [--ignore FIELD[:BIT] ...] [--streaks 10] [--json out.json]
       py tools/parity_compare.py --self-test

How it pairs: a friend line says which moment of the host's game its playback showed (t, host clock) and which host
room instance it follows; it is paired with the host line of that room instance nearest in t. In each pair, every
host enemy is matched with the friend's enemy bound to it (same host enemy number).

What it reports, per enemy type (model):
  - for each field, how often the two games agreed; flag fields (col, f120, f9b8, pat, f8c8) bit by bit, cl byte
    by byte (f8c8 bit 6 = a Shadow's puddle form);
  - mismatch streaks: a field that differs for longer than --min-ms plus the friend's playback delay, with the host
    log's time it started (use it to find the moment in the logs or a video);
  - host enemies the friend had no copy of for that long, and friend enemies left visible with no host match;
  - the friend's hits the host applied (host log 'world: friend hit our#N').
Fields the mirror itself writes on the friend (pos, mot, hp) only show that it was applied; the others (col, cl,
f120, f9b8, fade, pat, f8c8) are what each game's own scripts, or with one brain the host's replayed calls, left. thr
(script threads) is listed but never fails a type: with one brain the friend's threads don't run.
PASS for a type = no streak and no missing copy longer than the limit, in any field that isn't --ignore'd.
"""
import argparse
import bisect
import json
import math
import os
import re
import statistics
import sys
import tempfile

LINE = re.compile(r"^(\d\d:\d\d:\d\d\.\d\d\d) parity: ([HF]) (.*)$")
HIT = re.compile(r"^(\d\d:\d\d:\d\d\.\d\d\d) world: friend hit our#(\d+) (\S+) for (-?\d+)")
# Logs before session 35 name the types ("; types: ..."); since then one brain is for every type.
TYPES = re.compile(r"world mirror \((?:experiment, )?one brain\): .*?(?:; types: (.*))?$")
PERIOD = re.compile(r"parity sampler on, every (\d+) ms")
FLAGS32 = ("f120", "f9b8", "pat", "f8c8")
INFO_ONLY = {"thr"}


def parse_kv(text):
    out = {}
    for tok in text.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
    return out


def parse_entry(text):
    kv = parse_kv(text)
    e = {"id": int(kv["id"]), "obj": kv.get("obj", "?"), "ser": int(kv.get("ser", 0)), "hp": int(kv.get("hp", -1)),
         "pos": tuple(float(v) for v in kv.get("pos", "0,0,0").split(",")), "mot": int(kv.get("mot", -1)),
         "bank": int(kv.get("bank", 0)), "col": int(kv.get("col", "0"), 16), "cl": int(kv.get("cl", "0"), 16),
         "f120": int(kv.get("f120", "0"), 16), "f9b8": int(kv.get("f9b8", "0"), 16),
         "fade": float(kv.get("fade", 0)), "pat": int(kv.get("pat", "0"), 16), "thr": int(kv.get("thr", 0)),
         "f8c8": int(kv.get("f8c8", "0"), 16), "sy": float(kv.get("sy", 1))}
    if "ours" in kv:
        e["ours"] = int(kv["ours"])
        e["st"] = kv.get("st", "-")
    return e


def read_log(path, role):
    """Samples of one role, the hits the host applied (host log), the one-brain types and the sample period."""
    samples, hits, types, period = [], [], None, None
    seg = None  # host: the room instance of the newest sample (hits belong to it)
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if "parity: " in line:
                m = LINE.match(line.rstrip("\n"))
                if not m or m.group(2) != role:
                    continue
                parts = m.group(3).split(" ; ")
                h = parse_kv(parts[0])
                try:
                    s = {"clock": m.group(1), "t": int(h["t"]), "room": h.get("room"), "upd": int(h.get("upd", 1)),
                         "n": int(h.get("n", 0)), "drop": int(h.get("drop", 0)),
                         "entries": [parse_entry(p) for p in parts[1:] if p.strip()]}
                except (KeyError, ValueError):
                    continue
                if role == "H":
                    s["inst"] = int(h["inst"])
                    seg = (s["room"], s["inst"])
                else:
                    s["hroom"], s["hinst"] = h.get("hroom"), int(h.get("hinst", -1))
                    s["lead"] = int(h.get("lead", 0))
                samples.append(s)
                continue
            m = HIT.match(line)
            if m and seg:
                hits.append({"clock": m.group(1), "seg": seg, "id": int(m.group(2)), "obj": m.group(3),
                             "damage": int(m.group(4))})
                continue
            m = TYPES.search(line)
            if m:
                types = (m.group(1) or "every type").strip()
                continue
            m = PERIOD.search(line)
            if m:
                period = int(m.group(1))
    return samples, hits, types, period


class Streaks:
    """Runs of consecutive paired samples where a key (enemy, field) disagreed."""

    def __init__(self, period):
        self.period = period
        self.open = {}  # key -> [first t, last t, first clock, samples]
        self.done = []  # (key, first t, last t, clock, samples)

    def bad(self, key, t, clock):
        s = self.open.get(key)
        if s and t - s[1] > 3 * self.period:  # a hole in the samples: a new run
            self.close(key)
            s = None
        if s:
            s[1] = t
            s[3] += 1
        else:
            self.open[key] = [t, t, clock, 1]

    def good(self, key):
        if key in self.open:
            self.close(key)

    def close(self, key):
        first, last, clock, n = self.open.pop(key)
        self.done.append((key, first, last, clock, n))

    def close_all(self):
        for key in list(self.open):
            self.close(key)

    def duration(self, s):
        return s[2] - s[1] + self.period


def bit_names(field, x):
    return [f"{field}:{b}" for b in range(32) if x >> b & 1]


def compare_entry(h, f, pos_tol):
    """Keys that disagree, and the field names compared (for the agreement counts)."""
    bad = []
    if h["hp"] != f["hp"]:
        bad.append("hp")
    if math.dist(h["pos"], f["pos"]) > pos_tol:
        bad.append("pos")
    if h["mot"] != f["mot"] or h["bank"] != f["bank"]:
        bad.append("mot")
    if abs(h["fade"] - f["fade"]) > 0.05:
        bad.append("fade")
    if abs(h["sy"] - f["sy"]) > 0.1:  # height scale: a Shadow squashes as it sinks
        bad.append("sy")
    if h["thr"] != f["thr"]:
        bad.append("thr")
    bad += bit_names("col", h["col"] ^ f["col"])
    x = h["cl"] ^ f["cl"]
    bad += [f"cl@{0x5A8 + k:X}" for k in range(8) if x >> (8 * k) & 0xFF]
    for field in FLAGS32:
        bad += bit_names(field, h[field] ^ f[field])
    return bad


FIELDS = ("hp", "pos", "mot", "fade", "sy", "thr", "col", "cl", "f120", "f9b8", "pat", "f8c8")


def field_of(key):
    return re.split("[:@]", key)[0]


def ignored(key, ignore):
    return key in ignore or field_of(key) in ignore


def compare(host, friend, period, min_ms, pos_tol, only_types=(), ignore=()):
    ignore = set(ignore)
    segs = {}
    for s in host:
        if s["upd"]:
            segs.setdefault((s["room"], s["inst"]), []).append(s)
    for v in segs.values():
        v.sort(key=lambda s: s["t"])
    seg_t = {k: [s["t"] for s in v] for k, v in segs.items()}
    pairs, dts, leads, unpaired_friend = [], [], [], 0
    for fs in friend:
        if not fs["upd"] or fs["room"] != fs["hroom"]:
            continue
        key = (fs["hroom"], fs["hinst"])
        ts = seg_t.get(key)
        if not ts:
            unpaired_friend += 1
            continue
        i = bisect.bisect_left(ts, fs["t"])
        best = min((j for j in (i - 1, i) if 0 <= j < len(ts)), key=lambda j: abs(ts[j] - fs["t"]))
        dt = fs["t"] - ts[best]
        if abs(dt) > period / 2:
            unpaired_friend += 1
            continue
        pairs.append((segs[key][best], fs))
        dts.append(dt)
        leads.append(fs["lead"])
    lead = statistics.median(leads) if leads else 0
    limit = min_ms + max(0, lead)

    streaks = Streaks(period)
    by_type = {}
    extras = 0

    def stats(obj):
        return by_type.setdefault(obj, {"samples": 0, "agree": {f: [0, 0] for f in FIELDS}, "bits": {},
                                        "missing": 0, "ids": set()})

    for hs, fs in pairs:
        seg = (hs["room"], hs["inst"])
        fbound = {e["id"]: e for e in fs["entries"] if e["id"]}
        extras += sum(1 for e in fs["entries"] if not e["id"] and "p" not in e["st"] and "k" not in e["st"]
                      and "v" not in e["st"])
        for h in hs["entries"]:
            if only_types and h["obj"] not in only_types:
                continue
            st = stats(h["obj"])
            st["ids"].add((seg, h["id"]))
            mkey = (seg, h["id"], h["obj"], "missing")
            if h["hp"] == 0:  # dying on the host: the friend's copy is being killed
                streaks.good(mkey)
                continue
            f = fbound.get(h["id"])
            if not f:
                st["missing"] += 1
                streaks.bad(mkey, hs["t"], hs["clock"])
                continue
            streaks.good(mkey)
            st["samples"] += 1
            bad = compare_entry(h, f, pos_tol)
            badset = set(bad)
            for field in FIELDS:
                st["agree"][field][1] += 1
                if not any(field_of(k) == field for k in bad):
                    st["agree"][field][0] += 1
            for k in bad:
                st["bits"][k] = st["bits"].get(k, 0) + 1
                streaks.bad((seg, h["id"], h["obj"], k), hs["t"], hs["clock"])
            for key in [k for k in streaks.open if k[:3] == (seg, h["id"], h["obj"]) and k[3] != "missing"]:
                if key[3] not in badset:
                    streaks.good(key)
    streaks.close_all()

    long_ones = [s for s in streaks.done if streaks.duration(s) > limit]
    report = {"pairs": len(pairs), "host_samples": len(host), "friend_samples": len(friend),
              "friend_unpaired": unpaired_friend, "dt_median": statistics.median(dts) if dts else None,
              "dt_max": max((abs(d) for d in dts), default=None), "lead_median": lead, "limit_ms": limit,
              "visible_extras": extras, "types": {}}
    for obj, st in sorted(by_type.items()):
        mine = [s for s in long_ones if s[0][2] == obj]
        failing = [s for s in mine if not ignored(s[0][3], ignore) and s[0][3] not in INFO_ONLY]
        report["types"][obj] = {
            "enemies": len(st["ids"]), "paired_samples": st["samples"], "missing_samples": st["missing"],
            "agree": {f: (a / n if n else None) for f, (a, n) in st["agree"].items()},
            "bits": dict(sorted(st["bits"].items(), key=lambda kv: -kv[1])),
            "streaks": [{"enemy": s[0][1], "seg": list(s[0][0]), "key": s[0][3], "start": s[3],
                         "ms": streaks.duration(s), "samples": s[4]}
                        for s in sorted(mine, key=lambda s: -streaks.duration(s))],
            "verdict": "FAIL" if failing else "PASS"}
    return report


def hits_by_type(hits, only_types=()):
    out = {}
    for h in hits:
        if only_types and h["obj"] not in only_types:
            continue
        d = out.setdefault(h["obj"], {"hits": 0, "damage": 0, "enemies": set()})
        d["hits"] += 1
        d["damage"] += h["damage"]
        d["enemies"].add((h["seg"], h["id"]))
    return out


def print_report(report, hits, types_h, types_f, period, nstreaks):
    print(f"pairs {report['pairs']} (host samples {report['host_samples']}, friend {report['friend_samples']}, "
          f"friend samples without a host match {report['friend_unpaired']}); sample period {period} ms")
    if report["pairs"]:
        print(f"time offset of the pairs: median {report['dt_median']} ms, worst {report['dt_max']} ms; friend's "
              f"playback delay (lead) median {report['lead_median']} ms -> streak limit {report['limit_ms']} ms")
    print(f"one brain types: host {types_h or 'off'}, friend {types_f or 'off'}"
          + ("   <-- DIFFERENT" if types_h != types_f else ""))
    print(f"friend samples with a visible enemy the host doesn't have: {report['visible_extras']}")
    for obj, t in report["types"].items():
        print(f"\n== {obj}: {t['verdict']}  ({t['enemies']} enemies, {t['paired_samples']} paired samples, "
              f"{t['missing_samples']} samples with no friend copy)")
        agree = ", ".join(f"{f} {v * 100:.0f}%" for f, v in t["agree"].items() if v is not None)
        print(f"   agreement: {agree}")
        if t["bits"]:
            print("   most often different: " + ", ".join(f"{k} x{n}" for k, n in list(t["bits"].items())[:8]))
        h = hits.get(obj)
        print(f"   friend's hits applied by the host: {h['hits']} on {len(h['enemies'])} enemies, {h['damage']} damage"
              if h else "   friend's hits applied by the host: none")
        for s in t["streaks"][:nstreaks]:
            what = "no friend copy" if s["key"] == "missing" else s["key"]
            note = " (info only)" if s["key"] in INFO_ONLY else ""
            print(f"   streak: enemy #{s['enemy']} (room {s['seg'][0]} instance {s['seg'][1]}) {what} for "
                  f"{s['ms']} ms from {s['start']} (host log), {s['samples']} samples{note}")
        if len(t["streaks"]) > nstreaks:
            print(f"   ... {len(t['streaks']) - nstreaks} more streaks")


def self_test():
    """Two made-up logs with known differences; checks that exactly those are found."""
    def entry(i, obj, pos, col=0x00, f120=0x800000, thr=2, extra=""):
        return (f" ; id={i} obj={obj} ser={70 + i} hp=40 pos={pos[0]},{pos[1]},{pos[2]} mot=5 bank=0 col={col:02X} "
                f"cl=0000000100000000 f120={f120:X} f9b8=483 fade=1.00 pat=0 thr={thr}{extra}")

    host, friend = [], []
    for k in range(40):  # 10 s, 3 Shadows; one Soldier
        t = 100000 + k * 250
        clock = f"21:00:{k // 4:02d}.{(k % 4) * 250:03d}"
        es = [(1, "M_EX020"), (2, "M_EX020"), (3, "M_EX020"), (4, "M_EX010")]
        hl = f"{clock} parity: H t={t + 3} room=0409 inst=2 seq={k} upd=40 sora=0,0,0,1 n=4 drop=0"
        fl = f"{clock} parity: F t={t + 9} room=0409 hroom=0409 hinst=2 seq={k} lead=120 upd=40 sora=0,0,0,1 n=4 drop=0"
        for i, obj in es:
            pos = (100 * i + k, -100, 0)
            hl += entry(i, obj, pos)
            if i == 3 and k >= 28:  # the friend loses Shadow 3 at 7 s
                continue
            col = 0x40 if (i == 2 and 12 <= k < 20) else 0x00  # 2 s collision bit 6 on enemy 2
            f120 = 0x800008 if (i == 1 and k == 5) else 0x800000  # one sample: under the limit
            thr = 0 if i == 4 else 2  # info only: never fails
            fl += entry(i, obj, pos, col=col, f120=f120, thr=thr, extra=f" ours={i + 10} st=bs")
        host.append(hl)
        friend.append(fl)
        if k == 10:
            host.append(f"{clock} world: friend hit our#1 M_EX020 for 12: HP 40 -> 28")
    d = tempfile.mkdtemp()
    hp, fp = os.path.join(d, "host.log"), os.path.join(d, "friend.log")
    open(hp, "w").write("00:00:00.000 world sync: parity sampler on, every 250 ms of the host's clock\n" +
                        "\n".join(host) + "\n")
    open(fp, "w").write("\n".join(friend) + "\n")
    hs, hits, _, _ = read_log(hp, "H")
    fs, _, _, _ = read_log(fp, "F")
    r = compare(hs, fs, 250, 500, 60)
    shadow, soldier = r["types"]["M_EX020"], r["types"]["M_EX010"]
    keys = sorted((s["enemy"], s["key"]) for s in shadow["streaks"])
    problems = []
    if r["pairs"] != 40:
        problems.append(f"pairs {r['pairs']}, expected 40")
    if keys != [(2, "col:6"), (3, "missing")]:
        problems.append(f"Shadow streaks {keys}, expected enemy 2 col:6 and enemy 3 missing")
    col = next((s for s in shadow["streaks"] if s["key"] == "col:6"), None)
    if col and (col["ms"] != 2000 or col["start"] != "21:00:03.000"):
        problems.append(f"col streak {col['ms']} ms from {col['start']}, expected 2000 ms from 21:00:03.000")
    if shadow["verdict"] != "FAIL" or soldier["verdict"] != "PASS":
        problems.append(f"verdicts {shadow['verdict']} / {soldier['verdict']}, expected FAIL / PASS")
    if soldier["agree"]["thr"] != 0.0 or not soldier["streaks"]:
        problems.append("the Soldier's thr difference should be listed (info only)")
    if r["limit_ms"] != 620:
        problems.append(f"limit {r['limit_ms']}, expected 620 (500 + lead 120)")
    hb = hits_by_type(hits)
    if hb.get("M_EX020", {}).get("hits") != 1:
        problems.append(f"hits {hb}, expected one on M_EX020")
    if compare(hs, fs, 250, 500, 60, ignore=["col", "missing"])["types"]["M_EX020"]["verdict"] != "PASS":
        problems.append("--ignore col missing should make the Shadows PASS")
    print("self-test: " + ("PASS" if not problems else "FAIL\n  " + "\n  ".join(problems)))
    return not problems


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host_log", nargs="?")
    ap.add_argument("friend_log", nargs="?")
    ap.add_argument("--min-ms", type=int, default=500, help="a difference shorter than this (+ delay) is lag")
    ap.add_argument("--pos-tol", type=float, default=60, help="positions closer than this agree")
    ap.add_argument("--type", action="append", default=[], help="only this model (e.g. M_EX020); repeatable")
    ap.add_argument("--ignore", action="append", default=[], help="field or field:bit that never fails a type")
    ap.add_argument("--streaks", type=int, default=10, help="streaks listed per type")
    ap.add_argument("--json", help="also write the report here")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        sys.exit(0 if self_test() else 1)
    if not a.host_log or not a.friend_log:
        ap.error("host and friend logs needed")
    host, hits, types_h, period_h = read_log(a.host_log, "H")
    friend, _, types_f, period_f = read_log(a.friend_log, "F")
    if not host or not friend:
        sys.exit(f"no parity lines: host {len(host)}, friend {len(friend)} (KH2COOP_PARITY=1 on both PCs?)")
    if period_h and period_f and period_h != period_f:
        print(f"warning: the sample periods differ (host {period_h} ms, friend {period_f} ms)")
    period = period_h or period_f or 250
    report = compare(host, friend, period, a.min_ms, a.pos_tol, a.type, a.ignore)
    hb = hits_by_type(hits, a.type)
    print_report(report, hb, types_h, types_f, period, a.streaks)
    if a.json:
        for obj, h in hb.items():
            report["types"].setdefault(obj, {})["friend_hits"] = {"hits": h["hits"], "damage": h["damage"],
                                                                  "enemies": len(h["enemies"])}
        with open(a.json, "w") as f:
            json.dump(report, f, indent=1)


if __name__ == "__main__":
    main()
