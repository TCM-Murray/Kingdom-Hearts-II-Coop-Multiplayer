"""Build kh2coop.dll and install it as the "KH2 Coop" OpenKH mod.

Usage: py tools/deploy.py

Copies the DLL (and Sora's motion set from tools/make_mset.py) to two places:
  1. <openkh>/mods/kh2/KH2 Coop/      the mod source; Mods Manager "Build" copies it from here
  2. <openkh>/mod/kh2/dll/            the built output Panacea loads from, so a DLL-only
                                      change works on the next game start without a Build
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

mod_src = OPENKH / "mods" / "kh2" / "KH2 Coop"
mod_src.mkdir(parents=True, exist_ok=True)
shutil.copy2(ROOT / "openkh_mod" / "mod.yml", mod_src / "mod.yml")
shutil.copy2(dll, mod_src / "kh2coop.dll")
assets = ROOT / "build" / "mod_assets" / "obj"  # Sora's and the Keyblade's sets + rescue motions (tools/make_mset.py)
MSETS = ("P_EX100.mset", "W_EX010.mset")
subprocess.run(["py", ROOT / "tools" / "make_mset.py", assets], check=True, stdout=subprocess.DEVNULL)
for name in MSETS:
    shutil.copy2(assets / name, mod_src / name)
print(f"mod source -> {mod_src}")

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
