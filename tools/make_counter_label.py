"""Make the Grim Reaper's counter labels SORA P1 and SORA P2 (TODO 1.291, src/dll/mission_sync.cpp's kPlayerCounters).

Usage: py tools/make_counter_label.py [out folder] [--sheet preview.png]
       (default out: build/mod_assets/labels, as the title labels; tools/deploy.py runs it)

The fight's counter 0 (the host's Sora's medals) draws its label, sprite 0 of its sequence, from texels 1,46 - 96,63
of its picture; the PC draws the HD copy remastered/msn/<lang>/CA01_MS204.bar/<LANG>_CA01_MS204_bar0.png (L2112).
This tool writes SORA P1 there, and SORA P2 LABEL_SHIFT texel rows lower, where nothing draws; our DLL gives the
friend's counter a copy of the sequence with sprite 0 moved down as many rows. The P is cut from PRESENTS (Christmas
Town's mission picture, the same font); the font has no digits in any mission picture, so 1 and 2 are the counter's
own digits (sprites 3 and 4) in the label's yellow, scaled to the letters' height.

Source of each file: what the game would show without our mod (make_title_label.py's rule). Nothing from the game
is kept in the repo: the PNGs are made on this PC (build/ is ignored by git).
"""
import pathlib
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from make_title_label import ALPHA_MIN, BASE, ROOT, mod_order, source_for  # noqa: E402

MISSION = "CA01_MS204.bar"
LABEL = (1, 46, 96, 63)  # texels: the label sprite (sprite 0 of ct_c_0.sqd, L2112)
LABEL_SHIFT = 19         # texel rows: kPlayerCounters' row in mission_sync.cpp moves the P2 label as much
FREE = (0, 64, 104, 85)  # texels no sprite of the sequence uses (L2112)
DIGITS = {"1": (26, 0, 49, 22), "2": (49, 0, 72, 22)}  # texels: sprites 3 and 4
P_SOURCE = "remastered/msn/us/NM00_MS202.bar/US_NM00_MS202_bar1.png"  # PRESENTS, label at the same texels
WORD_GAP = 5.5  # texels between SORA and P: about the font's word space
DIGIT_GAP = {"1": 2.0, "2": 1.0}  # texels between P and the digit (your looks on the bench, 2026-11-25: 'a bit
                                  # separated', then the 2 'too spacious': its curve starts further right, the rows'
                                  # mean gap was 29.9 HD pixels against the 1's 26.4)
DIGIT_RAISE = 3  # HD rows above the letters' baseline (yours: 'raise it 1-2 pixels'; 1.4 pixels in a bench window)
DIGIT_SIZE = 0.95  # the digit's yellow: 35 of the letters' 37 HD rows, one pixel less in a bench window (yours,
                   # 2026-11-25: 'a tad smaller', then '1 pixel smaller'; it was 19% taller at first)


def scaled(box, s):
    return tuple(round(v * s) for v in box)


def ink(a):
    return a[..., 3] > ALPHA_MIN


def face(a):  # the letters' yellow, not their dark shadow
    return ink(a) & (a[..., 0] > 200) & (a[..., 1] > 200) & (a[..., 2] < 90)


def cut_p(mods):  # PRESENTS' P with its shadow, cropped to its ink rows
    path = source_for(P_SOURCE, mods)
    if not path:
        sys.exit(f"no {P_SOURCE}")
    pic = Image.open(path).convert("RGBA")
    s = pic.width / 128
    a = np.asarray(pic.crop(scaled(LABEL, s)))
    per_col = face(a).sum(axis=0)
    cols = np.where(per_col > 0)[0]
    if not len(cols):
        sys.exit("no PRESENTS in " + str(path))
    start, top = cols[0], per_col.max()
    # P ends where R's stem begins: a column back at full height right after P's bowl
    end = next((c for c in range(start + 4, len(per_col)) if per_col[c] >= 0.9 * top and per_col[c - 1] < 0.6 * top), 0)
    if not end:
        sys.exit("can't find where PRESENTS' P ends")
    a = a[:, start:end].copy()
    f = face(a)
    a[..., 3][f & ~letter_at(f, 0)] = 0  # yellow that isn't P's: R's edge, which reaches back into P's columns
    rows = np.where(ink(a).any(axis=1))[0]
    return Image.fromarray(a[rows[0]:rows[-1] + 1])


def letter_at(mask, col):
    """The yellow pixels joined to those in column `col` (one letter's face; letters' faces don't touch)."""
    seen = np.zeros_like(mask)
    todo = [(y, col) for y in np.where(mask[:, col])[0]]
    while todo:
        y, x = todo.pop()
        if 0 <= y < mask.shape[0] and 0 <= x < mask.shape[1] and mask[y, x] and not seen[y, x]:
            seen[y, x] = True
            todo += [(y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)]
    return seen


def digit(pic, s, box, face_h, color):
    """The digit in the label's yellow, its face face_h tall; and its face's bottom row in the returned image."""
    a = np.asarray(pic.crop(scaled(box, s))).astype(float)
    rows, cols = np.where(ink(a.astype(np.uint8)))
    a = a[rows.min():rows.max() + 1, cols.min():cols.max() + 1]
    lum = a[..., :3].mean(axis=2)
    bright = lum > 160  # the white face; the dark shadow stays as it is
    a[bright, :3] = color * (lum[bright] / lum[bright].max())[:, None]
    face_rows = np.where(bright.any(axis=1))[0]
    k = face_h / (face_rows[-1] + 1 - face_rows[0])  # match faces, not shadows: the digits' shadow is thinner
    g = Image.fromarray(a.astype(np.uint8), "RGBA")
    g = g.resize((max(1, round(g.width * k)), max(1, round(g.height * k))), Image.LANCZOS)
    rows = np.where(face(np.asarray(g)).any(axis=1))[0]  # measured after the resize: its blur moves the edge
    return g, rows[-1] + 1


def relabel(pic, p_glyph):
    """The picture with SORA P1 in the label and SORA P2 LABEL_SHIFT rows lower. Exits on a picture it doesn't know."""
    s = pic.width / 128
    box = scaled(LABEL, s)
    a = np.asarray(pic)
    fx0, fy0, fx1, fy1 = scaled(FREE, s)
    if ink(a[fy0:fy1, fx0:fx1]).any():
        sys.exit("the rows under the label aren't empty: not the picture this tool knows")
    lab = pic.crop(box)
    la = np.asarray(lab)
    rows, cols = np.where(ink(la))
    word_end, top, bottom = cols.max() + 1, rows.min(), rows.max() + 1
    color = np.median(la[face(la)][:, :3], axis=0)
    height = bottom - top  # SORA's letters with their shadow: P is scaled to it
    face_rows = np.where(face(la).any(axis=1))[0]
    face_bottom, face_h = face_rows[-1] + 1, face_rows[-1] + 1 - face_rows[0]
    p = p_glyph.resize((round(p_glyph.width * height / p_glyph.height), height), Image.LANCZOS)

    def line(d):
        out = lab.copy()
        x = word_end + round(WORD_GAP * s)
        out.alpha_composite(p, (x, top))
        x += p.width + round(DIGIT_GAP[d] * s)
        g, g_face_bottom = digit(pic, s, DIGITS[d], round(face_h * DIGIT_SIZE), color)
        y = face_bottom - g_face_bottom - DIGIT_RAISE  # its face's bottom just above the letters' baseline
        if x + g.width > out.width or y < 0 or y + g.height > out.height:
            sys.exit(f"SORA P{d} doesn't fit the label sprite")
        out.alpha_composite(g, (x, y))
        return out

    out = pic.copy()
    out.paste(line("1"), box[:2])  # paste, not composite: the line already holds SORA
    out.paste(line("2"), (box[0], box[1] + round(LABEL_SHIFT * s)))
    return out


def main():
    args = sys.argv[1:]
    sheet = None
    if "--sheet" in args:
        i = args.index("--sheet")
        sheet = pathlib.Path(args[i + 1])
        del args[i:i + 2]
    out_dir = pathlib.Path(args[0]) if args else ROOT / "build" / "mod_assets" / "labels"
    mods = mod_order()
    p_glyph = cut_p(mods)
    made = []
    for lang_dir in sorted(p for p in (BASE / "msn").iterdir() if (p / MISSION).is_file()):
        lang = lang_dir.name
        rel = f"remastered/msn/{lang}/{MISSION}/{lang.upper()}_{MISSION.replace('.bar', '')}_bar0.png"
        src = source_for(rel, mods)
        if not src:
            print(f"{lang}: no {rel}, skipped")
            continue
        pic = relabel(Image.open(src).convert("RGBA"), p_glyph)
        dest = out_dir / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        pic.save(dest)
        made.append(pic)
        print(f"{lang}: {dest}")
    if not made:
        sys.exit("no counter picture found")
    if sheet:
        w = made[0].width
        img = Image.new("RGBA", (w * len(made), made[0].height), (60, 60, 90, 255))
        for k, pic in enumerate(made):
            img.alpha_composite(pic, (k * w, 0))
        img.save(sheet)


if __name__ == "__main__":
    main()
