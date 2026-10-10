"""Build kh2coop.dll and install it as the "KH2 Coop" OpenKH mod.

Usage: py tools/deploy.py

Copies the DLL (plus Sora's motion set from tools/make_mset.py and the title's LOAD (CO-OP)
label textures from tools/make_title_label.py) to two places:
  1. <openkh>/mods/kh2/KH2 Coop/      the mod source; Mods Manager "Build" copies it from here
  2. <openkh>/mod/kh2/                the built output Panacea loads from, so a change works
                                      on the next game start without a Build
The label textures replace files that texture mods (e.g. Texture_HD) also have: "KH2 Coop"
must sit above them in Mods Manager's list (top wins), or a Build puts theirs back.
mod.yml = openkh_mod/mod.yml + one entry per label texture (also saved as build/mod_assets/mod.yml).
"""
import pathlib
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent
OPENKH = pathlib.Path(r"D:\SteamLibrary\steamapps\common\KINGDOM HEARTS openkh")
CMAKE = pathlib.Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
                     r"\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe")

subprocess.run([CMAKE, "-S", ROOT, "-B", ROOT / "build", "-G", "Visual Studio 17 2022", "-A", "x64"],
               check=True, stdout=subprocess.DEVNULL)
subprocess.run([CMAKE, "--build", ROOT / "build", "--config", "Release"], check=True)
dll = ROOT / "build" / "Release" / "kh2coop.dll"

labels = ROOT / "build" / "mod_assets" / "labels"  # title LOAD (CO-OP) textures (tools/make_title_label.py)
if labels.exists():
    shutil.rmtree(labels)
subprocess.run(["py", ROOT / "tools" / "make_title_label.py", labels], check=True, stdout=subprocess.DEVNULL)
LABELS = sorted(p.relative_to(labels).as_posix() for p in labels.rglob("*.png"))
mod_yml = (ROOT / "openkh_mod" / "mod.yml").read_text(encoding="utf-8")
for rel in LABELS:
    win = rel.replace("/", "\\")
    mod_yml += (f"- name: {win}\n  method: copy\n  platform: pc\n  package: kh2_first\n"
                f"  source:\n  - name: {win}\n")
(ROOT / "build" / "mod_assets" / "mod.yml").write_text(mod_yml, encoding="utf-8")

mod_src = OPENKH / "mods" / "kh2" / "KH2 Coop"
mod_src.mkdir(parents=True, exist_ok=True)
(mod_src / "mod.yml").write_text(mod_yml, encoding="utf-8")
shutil.copy2(dll, mod_src / "kh2coop.dll")
if (mod_src / "remastered").exists():
    shutil.rmtree(mod_src / "remastered")
for rel in LABELS:
    (mod_src / rel).parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(labels / rel, mod_src / rel)
assets = ROOT / "build" / "mod_assets" / "obj"  # Sora's and the Keyblade's sets + rescue motions (tools/make_mset.py)
MSETS = ("P_EX100.mset", "W_EX010.mset")
subprocess.run(["py", ROOT / "tools" / "make_mset.py", assets], check=True, stdout=subprocess.DEVNULL)
for name in MSETS:
    shutil.copy2(assets / name, mod_src / name)
print(f"mod source -> {mod_src} ({len(LABELS)} title label textures)")

order_file = OPENKH / "mods-KH2.txt"
order = [l.strip() for l in order_file.read_text(encoding="utf-8").splitlines() if l.strip()] if order_file.exists() else []
if "KH2 Coop" in order:
    above = [m for m in order[:order.index("KH2 Coop")]
             if any((OPENKH / "mods" / "kh2" / m / rel).is_file() for rel in LABELS)]
    if above:
        print("WARNING: in Mods Manager, move \"KH2 Coop\" above " + ", ".join(above) +
              " (top wins), then Build: until then a Build shows their title labels, not LOAD (CO-OP)")

built = OPENKH / "mod" / "kh2"
if built.is_dir():
    (built / "dll").mkdir(exist_ok=True)
    try:
        shutil.copy2(dll, built / "dll" / "kh2coop.dll")
        print(f"built output -> {built / 'dll'}")
    except PermissionError:
        print("built output: DLL is in use (game running?); close the game and deploy again")
    (built / "obj").mkdir(exist_ok=True)
    for name in MSETS:
        try:
            shutil.copy2(assets / name, built / "obj" / name)
            print(f"built output -> {built / 'obj' / name}")
        except PermissionError:
            print(f"built output: {name} is in use (game running?); close the game and deploy again")
    copied = 0
    for rel in LABELS:
        try:
            (built / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(labels / rel, built / rel)
            copied += 1
        except PermissionError:
            print(f"built output: {rel} is in use (game running?); close the game and deploy again")
    print(f"built output -> {copied} title label textures in {built / 'remastered' / 'menu'}")
