"""Step 0 of docs/EFFECT_SYNC_PLAN.md: which live effects one game shows and the other doesn't, and which it doubles.

Usage: py tools/fx_diff.py <fx_sync0.json>      (written by test_extra_copy.py fx_sync0)

The input holds, per phase (who attacks) and game ("A" host, "B" friend), the average number of live effects per key
(owner, effect id, the actor it is attached to) over the bench's samples (bench.FxCensus). Owners and attachments are
named by role so both games line up: "Sora A" / "Sora B" (each player's Sora: its own game's, or the copy in the
other), else the model name. Effect id -1 = no start record (not started through an actor's effect list).
"""
import json
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


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    with open(sys.argv[1], encoding="utf-8") as fh:
        print("\n".join(report(json.load(fh))))


if __name__ == "__main__":
    main()
