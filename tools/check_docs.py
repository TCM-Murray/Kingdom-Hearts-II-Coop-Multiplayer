#!/usr/bin/env python3
"""Check that the project's documents keep their shape.

Run from anywhere:  py tools/check_docs.py  (add --strict to turn warnings into errors)

Errors (exit 1): a broken table, a duplicate or badly marked TODO item, README counts that
don't match TODO.md.  Warnings (exit 0): files over their size budget, TODO items or
VERIFIED_OFFSETS cells that run long, code constants with no entry in VERIFIED_OFFSETS.md.
"""
import re, sys, glob
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUDGET_KB = {"README.md": 15, "TODO.md": 60, "VERIFIED_OFFSETS.md": 100}
TODO_ITEM_MAX = 500          # characters per item line (about four rendered lines)
VO_CELL_MAX = 300            # characters per cell in the keyed sections
WEIGHT = {"P0": 3, "P1": 2, "P2": 1}

errors, warnings = [], []

def read(name):
    return (ROOT / name).read_text(encoding="utf-8")

def cells(line):
    """Split a markdown table row, honouring backticks and escaped pipes."""
    out, cur, tick, i = [], [], False, 0
    while i < len(line):
        ch = line[i]
        if ch == "\\" and i + 1 < len(line) and line[i + 1] == "|":
            cur.append("|"); i += 2; continue
        if ch == "`": tick = not tick
        if ch == "|" and not tick: out.append("".join(cur).strip()); cur = []
        else: cur.append(ch)
        i += 1
    out.append("".join(cur).strip())
    return out[1:-1]

def check_tables(name, text):
    expect = None
    for n, line in enumerate(text.split("\n"), 1):
        if line.startswith("|---"):
            expect = len(cells(text.split("\n")[n - 2])); continue
        if line.startswith("|") and expect is not None:
            if len(cells(line)) != expect:
                errors.append("%s:%d: table row has %d cells, header has %d" % (name, n, len(cells(line)), expect))
        elif not line.startswith("|"):
            expect = None

def check_sizes():
    for name, kb in BUDGET_KB.items():
        size = (ROOT / name).stat().st_size / 1024
        if size > kb:
            warnings.append("%s is %.0f KB, budget %d KB: time to trim (snapshot to docs/ first)" % (name, size, kb))

def todo_counts(text):
    counts = {p: {"done": 0, "built": 0, "open": 0} for p in WEIGHT}
    done_w = total_w = 0.0
    seen = set()
    for n, line in enumerate(text.split("\n"), 1):
        m = re.match(r"^\s*- \[(.)\] (~~)?\*\*(\d+\.\d+[a-z]?)\s*(P0|P1|P2|P0/P1)?", line)
        if not m:
            if re.match(r"^\s*- \[", line):
                errors.append("TODO.md:%d: item line doesn't follow '- [ ] **N.N P0. Title.**'" % n)
            continue
        mark, struck, num, pri = m.groups()
        if mark not in " x~":
            errors.append("TODO.md:%d: marker '[%s]' is not one of [ ] [~] [x]" % (n, mark))
        if num in seen:
            errors.append("TODO.md:%d: item %s appears twice" % (n, num))
        seen.add(num)
        if len(line) > TODO_ITEM_MAX:
            warnings.append("TODO.md:%d: item %s is %d characters (max %d): move the story to MODLOG / TODO_HISTORY" % (n, num, len(line), TODO_ITEM_MAX))
        if struck or not pri:
            continue
        pri = "P0" if pri == "P0/P1" else pri
        state = {" ": "open", "x": "done", "~": "built"}[mark]
        counts[pri][state] += 1
        total_w += WEIGHT[pri]
        done_w += WEIGHT[pri] * {"done": 1, "built": 0.5, "open": 0}[state]
    return counts, (100 * done_w / total_w if total_w else 0)

def check_readme(counts, pct):
    text = read("README.md")
    for pri, label in (("P0", "P0 (blocks"), ("P1", "P1 (needed"), ("P2", "P2 (polish")):
        m = re.search(r"^\| " + re.escape(label) + r"[^|]*\| *(\d+) *\| *(\d+) *\| *(\d+) *\|", text, re.M)
        if not m:
            errors.append("README.md: no progress-table row starting with '%s'" % label); continue
        got = tuple(int(x) for x in m.groups())
        want = (counts[pri]["done"], counts[pri]["built"], counts[pri]["open"])
        if got != want:
            errors.append("README.md: %s row says %s, TODO.md counts %s (done / built / open)" % (pri, got, want))
    m = re.search(r"\*\*Progress: about (\d+)%\*\*", text)
    if m and abs(int(m.group(1)) - round(pct)) > 1:
        errors.append("README.md: progress says %s%%, TODO.md weighs %.0f%%" % (m.group(1), pct))

def check_offsets():
    text = read("VERIFIED_OFFSETS.md")
    check_tables("VERIFIED_OFFSETS.md", text)
    # long cells in the keyed sections (1 to 4)
    section = ""
    for n, line in enumerate(text.split("\n"), 1):
        if line.startswith("## "): section = line
        if section[:5] in ("## 1.", "## 2.", "## 3.", "## 4.") and line.startswith("| ") and not line.startswith("| Address") and not line.startswith("| Field") and not line.startswith("| Item"):
            for c in cells(line):
                if len(c) > VO_CELL_MAX:
                    warnings.append("VERIFIED_OFFSETS.md:%d: a cell is %d characters (max %d): keep the fact, point to the history" % (n, len(c), VO_CELL_MAX)); break
    # every exe+ constant the DLL names must have an entry: a keyed row, a mention in sections 1 to 6,
    # or an address-index row that points at entries ("Mentioned in" not "none"). Index rows that say
    # "none" and the Gaps list are made from the code's constants and don't count.
    head, _, tail = text.partition("\n## 7. Address index")
    tail = tail.split("\n## Gaps")[0]
    text = head + "\n" + "\n".join(l for l in tail.split("\n") if l.startswith("| ") and not l.rstrip().endswith("| none |"))
    for f in sorted(glob.glob(str(ROOT / "src" / "dll" / "*.cpp")) + glob.glob(str(ROOT / "src" / "dll" / "*.hpp"))):
        src = Path(f).read_text(encoding="utf-8", errors="replace")
        for m in re.finditer(r"constexpr\s+[\w:<>\s\*]+?\b(k\w+)\s*=\s*0x([0-9A-Fa-f]{5,7})\s*;", src):
            addr = int(m.group(2), 16)
            if addr < 0x100000: continue
            if not re.search(r"exe\s*\+\s*0x0*%X\b" % addr, text, re.I):
                warnings.append("VERIFIED_OFFSETS.md: no entry for exe+0x%X (%s in %s)" % (addr, m.group(1), Path(f).name))

def main():
    strict = "--strict" in sys.argv
    check_sizes()
    todo = read("TODO.md")
    check_tables("TODO.md", todo)
    counts, pct = todo_counts(todo)
    check_tables("README.md", read("README.md"))
    check_readme(counts, pct)
    check_offsets()
    for w in warnings: print("warning:", w)
    for e in errors: print("ERROR:", e)
    print("TODO counts: " + ", ".join("%s %d/%d/%d" % (p, c["done"], c["built"], c["open"]) for p, c in counts.items()) + " (done/built/open), %.0f%% weighted" % pct)
    bad = errors or (strict and warnings)
    print("check_docs: %s (%d errors, %d warnings)" % ("FAIL" if bad else "OK", len(errors), len(warnings)))
    sys.exit(1 if bad else 0)

if __name__ == "__main__":
    main()
