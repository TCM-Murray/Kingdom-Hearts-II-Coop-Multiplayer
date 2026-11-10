"""Step 0 of docs/EFFECT_SYNC_PLAN.md: which live effects one game shows and the other doesn't, and which it doubles.

Usage: py tools/fx_diff.py <fx_sync0.json>      (written by test_extra_copy.py fx_sync0)
       py tools/fx_diff.py <fx_sync1.json>      (fx_sync1: effects made per phase, by cause and file: made_report)
       py tools/fx_diff.py --selftest

The input holds, per phase (who attacks) and game ("A" host, "B" friend), the average number of live effects per key
(owner, effect id, the actor it is attached to) over the bench's samples (bench.FxCensus). Owners and attachments are
named by role so both games line up: "Sora A" / "Sora B" (each player's Sora: its own game's, or the copy in the
other), else the model name. Effect id -1 = no start record (not started through an actor's effect list).
"""
import json
import re
import sys

SEEN = 0.05  # average live count under this = not seen (one effect for 1 of 20 samples)
DOUBLE = 1.8  # one game's average this many times the other's = doubled


def report(phases):
    """Lines: per phase, the keys missing in each game, then the doubled ones."""
    out = []
    for phase, games in phases.items():
        a, b = games["A"], games["B"]
        rows = [(k, a.get(k, 0.0), b.get(k, 0.0)) for k in sorted(set(a) | set(b))]
        out.append(f"== {phase}: {len(rows)} kinds of live effect")
        for title, pick in (("in the host's game only (missing in the friend's)", lambda x, y: x >= SEEN and y < SEEN / 5),
                            ("in the friend's game only (missing in the host's)", lambda x, y: y >= SEEN and x < SEEN / 5),
                            ("doubled in the friend's game", lambda x, y: x >= SEEN and y >= DOUBLE * x),
                            ("doubled in the host's game", lambda x, y: y >= SEEN and x >= DOUBLE * y)):
            hits = sorted((r for r in rows if pick(r[1], r[2])), key=lambda r: -max(r[1], r[2]))
            out.append(f"  {title}: {len(hits)}")
            out += [f"    {k:70s} host {x:6.2f}  friend {y:6.2f}" for k, x, y in hits[:25]]
    return out


# What made an effect: the caller of exe+0x2C6BD0 in the count lines' "<- 0x..." (F394).
CAUSES = {0x40DF01: "motion trigger", 0x3B66AC: "actor effect start", 0x3B6702: "script effect start",
          0x2D9411: "motion's own effects", 0x3F6466: "landed hit"}


def made_report(phases):
    """Lines for effects made (counts, test_extra_copy.py fx_sync1): per phase, kinds made in one game only, kinds made
    in both from other files (the copy in a form reading normal Sora's effect file), and kinds made twice as often."""
    out = []
    for phase, games in phases.items():
        sides = []
        for g in ("A", "B"):
            kinds = {}
            for key, n in games[g].items():
                base, _, file = key.partition(" file ")
                cause = re.search(r"<- 0x([0-9A-F]+)$", base)
                if cause and int(cause.group(1), 16) in CAUSES:
                    base += f" ({CAUSES[int(cause.group(1), 16)]})"
                total, files = kinds.get(base, (0, set()))
                kinds[base] = (total + n, files | {file or "-"})
            sides.append(kinds)
        a, b = sides
        out.append(f"== {phase}: made host {sum(n for n, _ in a.values())}, friend {sum(n for n, _ in b.values())}")
        rows = [(k, a.get(k, (0, set())), b.get(k, (0, set()))) for k in sorted(set(a) | set(b))]
        for title, pick in (("made in the host's game only", lambda x, y: x[0] >= 2 and not y[0]),
                            ("made in the friend's game only", lambda x, y: y[0] >= 2 and not x[0]),
                            ("made in both, from other files", lambda x, y: x[0] and y[0] and x[1] != y[1]),
                            ("made twice as often in one game", lambda x, y: x[0] and y[0] and max(x[0], y[0]) >= 4
                             and max(x[0], y[0]) >= 2 * min(x[0], y[0]))):
            hits = sorted((r for r in rows if pick(r[1], r[2])), key=lambda r: -max(r[1][0], r[2][0]))
            out.append(f"  {title}: {len(hits)}")
            out += [f"    {k:90s} host {x[0]:4d} {','.join(sorted(x[1])):24s} friend {y[0]:4d} {','.join(sorted(y[1]))}"
                    for k, x, y in hits[:30]]
    return out


def _selftest():
    k = "own Sora A entry 5 from exe+0x2C6C10 <- 0x40DF01"
    lines = made_report({"host Valor": {"A": {k + " file obj/P_EX100_BTLF.a.us": 9, "limit - entry 1 from exe+0x1 "
                                              "<- 0x2 file -": 3},
                                        "B": {k + " file obj/P_EX100.a.us": 8}}})
    assert any("from other files: 1" in l for l in lines), lines
    assert any("host's game only: 1" in l for l in lines), lines
    assert any("(motion trigger)" in l and "P_EX100_BTLF" in l for l in lines), lines
    print("fx_diff self-check: OK")


def main():
    if sys.argv[1:] == ["--selftest"]:
        return _selftest()
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    with open(sys.argv[1], encoding="utf-8") as fh:
        data = json.load(fh)
    print("\n".join(made_report(data) if "fx_sync1" in sys.argv[1] else report(data)))


if __name__ == "__main__":
    main()
