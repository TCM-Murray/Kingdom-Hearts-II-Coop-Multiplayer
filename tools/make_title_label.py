"""Make the title screen's LOAD (CO-OP) label (TODO 5.7, src/dll/title_menu.cpp) in every game language.

Usage: py tools/make_title_label.py [out folder] [--sheet preview.png] [--no-mods]
       (--no-mods: only the game's own textures, as without any texture mod)
       (default out: build/mod_assets; writes remastered/menu/<lang>/title.2ld/<name>.png)

The title's row labels are pictures in title texture 8 (and its copies 9, 10, 11, which differ
only in the GAME SETTINGS key icon); the PC draws them from the HD PNGs
remastered/menu/<lang>/title.2ld/<prefix>title_2ld<n>.png ("-<n>.png" = English in fm/).
Our DLL turns the THEATER row into LOAD (CO-OP), so this tool redraws that row's picture
(UV y 80-100 of 256) in each file with the words of that file's language. The game picks the
file of its own language, so the label follows the game's language by itself.

Source of each file: what the game would show without our mod: the highest mod in Mods
Manager's list (mods-KH2.txt, top wins) that has it (e.g. the Texture_HD mod), else the game's
own files (OpenKH data folder). So it works with or without texture mods; run it again
(tools/deploy.py does) after changing texture mods.
The letters are cut from the labels in the same texture set (all languages of it, so P, K etc.
are found); "(", ")" and "-" are built from parts of C and L. Nothing from the game is kept in
the repo: the PNGs are made on this PC (build/ is ignored by git).
"""
import pathlib
import re
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
OPENKH = pathlib.Path(r"D:\SteamLibrary\steamapps\common\KINGDOM HEARTS openkh")
BASE = OPENKH / "data" / "kh2"
OUR_MOD = "KH2 Coop"

# Row rectangles on the 256x256 texture (SEQD #7 frames 0, 2, 4, 26; VERIFIED_OFFSETS 'Title label textures')
ROWS = {"new": (33, 53), "load": (57, 77), "theater": (80, 100), "back": (133, 153)}
TARGET_ROW = "theater"
TEXTS = {  # the game's own labels per language (read off the textures, 2026-10-10)
    "EN": {"new": "NEW GAME", "load": "LOAD", "theater": "THEATER", "back": "BACK"},
    "FR": {"new": "NOUVELLE PARTIE", "load": "CHARGER UNE PARTIE", "theater": "THÉÂTRE", "back": "RETOUR"},
    "DE": {"new": "NEUES SPIEL", "load": "LADEN", "theater": "KINO", "back": "ZURÜCK"},
    "IT": {"new": "NUOVO GIOCO", "load": "CARICA", "theater": "TEATRO", "back": "INDIETRO"},
    "ES": {"new": "NUEVA PARTIDA", "load": "CARGAR", "theater": "ESCENAS", "back": "ATRÁS"},
}
LABEL = {"EN": "LOAD (CO-OP)", "FR": "CHARGER (CO-OP)", "DE": "LADEN (KOOP)", "IT": "CARICA (CO-OP)",
         "ES": "CARGAR (CO-OP)"}
PREFIX_LANG = {"-": "EN", "US_": "EN", "UK_": "EN", "FR_": "FR", "GR_": "DE", "IT_": "IT", "SP_": "ES"}
NAME_RE = re.compile(r"^(-|[A-Z]{2}_title_2ld)(8|9|10|11)\.png$")
ALPHA_MIN = 24  # ink threshold


def mod_order():
    path = OPENKH / "mods-KH2.txt"
    names = [l.strip() for l in path.read_text(encoding="utf-8").splitlines() if l.strip()] if path.exists() else []
    return [n for n in names if n != OUR_MOD]


def source_for(rel, mods):
    """The file the game would load for `rel` without our mod (Mods Manager: top of the list wins)."""
    for m in mods:
        p = OPENKH / "mods" / "kh2" / m / rel
        if p.is_file():
            return p
    p = BASE / rel
    return p if p.is_file() else None


def prefix_of(name):
    return "-" if name.startswith("-") else name[:3]


def load(path):
    im = Image.open(path).convert("RGBA")
    return np.array(im), im.width / 256.0


def runs(ink_cols, min_gap):
    """Column runs of ink, joining runs closer than min_gap (accents, thin joins)."""
    out, start, last = [], None, None
    for x, v in enumerate(ink_cols):
        if not v:
            continue
        if start is None:
            start = x
        elif x - last > min_gap:
            out.append((start, last + 1))
            start = x
        last = x
    if start is not None:
        out.append((start, last + 1))
    return out


def row_glyphs(img, scale, row, text):
    """([(char, glyph RGBA trimmed to its ink)], runs, (ink top, ink bottom) in the row) or None
    when the cut doesn't match the text."""
    y0, y1 = (int(round(v * scale)) for v in ROWS[row])
    band = img[y0:y1]
    cols = (band[:, :, 3] > ALPHA_MIN).any(axis=0)
    rs = runs(cols, max(1, int(scale * 0.75)))
    chars = text.replace(" ", "")
    # Some textures (UK) have stray Japanese labels on the right of a row: drop runs after a wide gap.
    if len(rs) > len(chars) and rs[len(chars)][0] - rs[len(chars) - 1][1] > 8 * scale:
        rs = rs[:len(chars)]
    if len(rs) != len(chars):
        return None
    out, tops, bottoms = [], [], []
    for c, (a, b) in zip(chars, rs):
        a, b = max(0, a - 1), min(band.shape[1], b + 1)  # keep the soft edge
        g = band[:, a:b]
        ink = (g[:, :, 3] > 4).any(axis=1).nonzero()[0]
        top, bottom = int(ink.min()), int(ink.max()) + 1
        out.append((c, g[top:bottom].copy()))
        if c.isascii():
            tops.append(top)
            bottoms.append(bottom)
    return out, rs, (int(np.median(tops)), int(np.median(bottoms)))


def spacing(rs, text):
    """(letter gaps, word gaps) in pixels from one row's runs."""
    gaps, words = [], []
    i = 0
    split = text.split(" ")
    for w, word in enumerate(split):
        for k in range(len(word) - 1):
            gaps.append(rs[i + k + 1][0] - rs[i + k][1])
        i += len(word)
        if w < len(split) - 1:
            words.append(rs[i][0] - rs[i - 1][1])
    return gaps, words


def to_height(g, h):
    """Glyph scaled to cap height h (width in proportion)."""
    if g.shape[0] == h:
        return g
    w = max(1, round(g.shape[1] * h / g.shape[0]))
    return np.array(Image.fromarray(g).resize((w, h), Image.LANCZOS))


class Glyphs:
    def __init__(self):
        self.g = {}       # char -> glyph trimmed to its ink (letters are capitals: same height in a set)
        self.gaps, self.words, self.left = [], [], []
        self.cap = None   # (top, bottom) of the letters in the target row of the file being changed

    def add_file(self, path, lang, primary):
        img, s = load(path)
        caps = {}
        for row, text in TEXTS[lang].items():
            r = row_glyphs(img, s, row, text)
            if not r:
                continue
            glyphs, rs, cap = r
            caps[row] = cap
            for c, g in glyphs:
                if c.isascii() and c not in self.g:
                    self.g[c] = g
            if primary:
                gp, wd = spacing(rs, text)
                self.gaps += [v / s for v in gp]
                self.words += [v / s for v in wd]
                self.left.append(rs[0][0] / s)
        if primary:
            self.cap = caps.get(TARGET_ROW) or caps.get("load") or next(iter(caps.values()), None)

    def get(self, c, h):
        if c in self.g:
            return to_height(self.g[c], h)
        if c in "()":
            base = self.get("C", h)
            half = base[:, : max(2, int(base.shape[1] * 0.42))]
            return half if c == "(" else half[:, ::-1].copy()
        if c == "-":
            l = self.get("L", h)
            ink = l[:, :, 3] > ALPHA_MIN
            full = [y for y in range(l.shape[0]) if ink[y].mean() > 0.6]  # L's bottom bar
            bar = l[min(full):max(full) + 1] if full else l[-2:]
            bar = bar[:, : max(2, int(l.shape[1] * 0.7))]
            out = np.zeros((h, bar.shape[1], 4), np.uint8)
            mid = h // 2 - bar.shape[0] // 2
            out[mid:mid + bar.shape[0]] = bar
            return out
        raise KeyError(c)


def compose(target_img, s, glyphs, label):
    y0, y1 = (int(round(v * s)) for v in ROWS[TARGET_ROW])
    height = y1 - y0
    top, bottom = glyphs.cap if glyphs.cap else (int(3 * s / 4), int(height - 3 * s / 4))
    cap = bottom - top
    gap = (float(np.median(glyphs.gaps)) if glyphs.gaps else 3.0) * s
    word = float(np.median(glyphs.words)) * s if glyphs.words else gap * 3
    left = float(min(glyphs.left)) * s if glyphs.left else 0.0
    parts = [None if c == " " else glyphs.get(c, cap) for c in label]

    def width(gp, wd):
        w, prev = 0.0, None
        for p in parts:
            if p is None:
                w += wd - gp
                continue
            if prev is not None:
                w += gp
            w += p.shape[1]
            prev = p
        return w

    room = 256 * s - left - s
    k = 1.0
    while width(gap * k, word * k) > room and k > 0.2:
        k -= 0.05
    total = width(gap * k, word * k)
    row = np.zeros((height, int(np.ceil(256 * s)), 4), np.uint8)
    x, prev = left, None
    for p in parts:
        if p is None:
            x += word * k - gap * k
            continue
        if prev is not None:
            x += gap * k
        xi = int(round(x))
        w = min(p.shape[1], row.shape[1] - xi)
        row[top:bottom, xi:xi + w] = np.maximum(row[top:bottom, xi:xi + w], p[:, :w])
        x += p.shape[1]
        prev = p
    if total > room:  # still too long: squeeze the whole label
        row = np.array(Image.fromarray(row).resize((int(row.shape[1] * room / total), height), Image.LANCZOS))
    out = target_img.copy()
    out[y0:y1, :] = 0
    out[y0:y1, : min(row.shape[1], out.shape[1])] = row[:, : out.shape[1]]
    return out


def targets(mods):
    """Every title label texture name the game has (rel paths under data/kh2), plus mods' extras."""
    found = set()
    roots = [BASE] + [OPENKH / "mods" / "kh2" / m for m in mods]
    for r in roots:
        for p in (r / "remastered" / "menu").glob("*/title.2ld/*.png"):
            if NAME_RE.match(p.name):
                found.add(p.relative_to(r).as_posix())
    return sorted(found)


def main():
    args = [a for a in sys.argv[1:]]
    sheet = None
    if "--sheet" in args:
        i = args.index("--sheet")
        sheet = pathlib.Path(args[i + 1])
        del args[i:i + 2]
    no_mods = "--no-mods" in args
    if no_mods:
        args.remove("--no-mods")
    out_root = pathlib.Path(args[0]) if args else ROOT / "build" / "mod_assets"
    mods = [] if no_mods else mod_order()
    made, previews = [], []
    for rel in targets(mods):
        name = pathlib.PurePosixPath(rel).name
        m = NAME_RE.match(name)
        lang = PREFIX_LANG.get(prefix_of(name))
        src = source_for(rel, mods)
        if not lang or not src:
            continue
        glyphs = Glyphs()
        glyphs.add_file(src, lang, primary=True)
        # Same texture set first (the source's folder, same copy number), then the game's own files.
        folder, n = src.parent, m.group(2)
        for p in sorted(folder.glob(f"*{n}.png")) + sorted((BASE / "remastered" / "menu").glob(f"*/title.2ld/*{n}.png")):
            pl = PREFIX_LANG.get(prefix_of(p.name))
            if pl and p != src and NAME_RE.match(p.name):
                glyphs.add_file(p, pl, primary=False)
        img, s = load(src)
        try:
            new = compose(img, s, glyphs, LABEL[lang])
        except KeyError as e:
            print(f"skipped {rel}: no letter {e} in this texture set")
            continue
        dst = out_root / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        Image.fromarray(new).save(dst)
        made.append(rel)
        y0, y1 = (int(round(v * s)) for v in (30, 156))
        previews.append(Image.fromarray(new[y0:y1]).resize((512, 252), Image.LANCZOS))
        src_note = src.relative_to(OPENKH)
        print(f"{rel}: {LABEL[lang]} from {src_note}")
    if sheet and previews:
        cols = 4
        out = Image.new("RGBA", (512 * cols, 252 * ((len(previews) + cols - 1) // cols)), (40, 40, 60, 255))
        for i, p in enumerate(previews):
            out.alpha_composite(p, ((i % cols) * 512, (i // cols) * 252))
        out.save(sheet)
    print(f"{len(made)} label textures -> {out_root}")


if __name__ == "__main__":
    main()
