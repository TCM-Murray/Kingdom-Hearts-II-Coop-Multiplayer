"""Put Chip's portrait into a free corner of the Gummi help bar's texture, for our co-op bar (src/dll/gummi_menu.cpp).

Usage: py tools/make_chip_portrait.py [out folder]
       (default out: build/mod_assets/labels; writes remastered/gumimenu/<lang>/gumimenu.bar/<name>.png)

The game's bar only has Dale (sprite 97, on texture 4). Our bar, and only ours, points that sprite at
SPOT, so the game's own help bar keeps Dale. Chip comes from the Gummi Editor's sheet (Dale left, Chip right).
Source of each file: what the game would show without our mod (see make_title_label.source_for), so
the PNGs are made on this PC and nothing from the game is kept in the repo.
"""
import pathlib
import sys

from PIL import Image

from make_title_label import BASE, ROOT, mod_order, source_for

SHEET = "remastered/gumimenu/us/gumiedit.bar/US_gumiedit_bar5.png"
CHIP_BOX = (229, 4, 452, 300)  # on the 512x512 sheet
SPOT = (340, 57, 384, 118)     # on the 512x256 texture; must match kChipUv in gummi_menu.cpp


def portrait(mods, size):
    """Chip fitted into `size`, bottom centered like Dale (his round cut sits on the bar's disc)."""
    sheet = source_for(SHEET, mods)
    if not sheet:
        sys.exit("no Gummi Editor sheet")
    chip = Image.open(sheet).convert("RGBA").crop(CHIP_BOX)
    k = min(size[0] / chip.width, size[1] / chip.height)
    fitted = chip.convert("RGBa").resize((round(chip.width * k), round(chip.height * k)), Image.LANCZOS)
    box = Image.new("RGBa", size)
    box.paste(fitted, ((size[0] - fitted.width) // 2, size[1] - fitted.height))
    return box.convert("RGBA")


def main():
    out = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "mod_assets" / "labels"
    mods = mod_order()
    names = sorted(p.relative_to(BASE).as_posix() for p in (BASE / "remastered" / "gumimenu").glob("*/gumimenu.bar/*_gumimenu_bar4.png"))
    if not names:
        sys.exit("no gumimenu textures found")
    for rel in names:
        src = source_for(rel, mods)
        img = Image.open(src).convert("RGBA")
        s = img.width // 512
        box = tuple(v * s for v in SPOT)
        if img.crop(box).getchannel("A").getextrema()[1] != 0:
            sys.exit(f"{src}: the spot isn't empty; pick another SPOT")
        size = (box[2] - box[0], box[3] - box[1])
        img.paste(portrait(mods, size), box[:2])
        dst = out / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        img.save(dst)
        print(f"{rel}: {img.width}x{img.height}")
    print(f"{len(names)} textures -> {out}")


if __name__ == "__main__":
    main()
