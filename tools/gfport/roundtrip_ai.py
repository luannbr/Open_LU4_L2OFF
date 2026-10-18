"""Checks decompile_ai.py: decompiles an ai.obj built by this server's NASC,
compiles the source again and compares the two objects.

usage: python roundtrip_ai.py <ai.obj> <script folder with the *_pch.txt files> <work folder>

The work folder is created (it must not exist). For the 2018 ai.obj in
script/ every class and handler must compare equal.
"""
import os
import shutil
import subprocess
import sys

tools = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(os.path.dirname(tools))
obj, script, work = sys.argv[1:4]
nasc = os.path.join(root, "build", "windows-msvc", "Release", "NASC", "NASC.exe")


def python(script_name, *args):
    subprocess.run([sys.executable, "-I", "-X", "utf8", os.path.join(tools, script_name), *args], check=True)


os.makedirs(work)
source_root = os.path.join(work, "root")
os.makedirs(os.path.join(source_root, "ai", "src"))
for name in os.listdir(script):
    if name.lower().endswith("_pch.txt") or name.lower().endswith("_pch2.txt") or name.lower().endswith("_pch3.txt"):
        shutil.copy2(os.path.join(script, name), source_root)

python("decompile_ai.py", obj, os.path.join(work, "src"), "--items", os.path.join(script, "item_pch.txt"))
for name in ("npc.ai", "maker.ai"):
    shutil.copy2(os.path.join(work, "src", name), os.path.join(source_root, "ai", "src", name))
result = subprocess.run([nasc, "--root", source_root, "--output", os.path.join(work, "out"),
                         "--module", "npc.ai", "--module", "maker.ai"], capture_output=True, text=True, errors="replace")
open(os.path.join(work, "nasc.log"), "w", encoding="utf-8").write(result.stdout + result.stderr)
if result.returncode != 0:
    sys.exit(f"NASC failed; see {os.path.join(work, 'nasc.log')}")
python("compare_obj.py", obj, os.path.join(work, "out", "ai.obj"), os.path.join(work, "compare.txt"))
