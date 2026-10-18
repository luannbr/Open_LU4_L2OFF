"""Converts the GF setting.txt for this server.

usage: python convert_setting.py <GF setting.txt> <2018 setting file> <output>

This server reads eight creation stats per class (INT STR CON MEN DEX WIT
LUC CHA); GF has the first six. LUC and CHA get the 2018 bounds (1 and 99)
and the 2018 recommended values of the same class.
"""
import re
import sys

gf_path, new_path, target = sys.argv[1], sys.argv[2], sys.argv[3]
gf = open(gf_path, encoding="utf-16").read()
new = open(new_path, encoding="utf-16", errors="replace").read()

def block(text, name):
    match = re.search(r"^\s*" + name + r"_begin\s*$(.*?)^\s*" + name + r"_end", text, re.S | re.M)
    return match

recommended = {}
for cls, values in re.findall(r"(\w+)\s*=\s*\{([^}]*)\}", block(new, "recommended_stat").group(1)):
    parts = values.split(";")
    if len(parts) == 8:
        recommended[cls] = parts[6:]

EXTRA = {"minimum_stat": lambda cls: ["1", "1"],
         "maximum_stat": lambda cls: ["99", "99"],
         "recommended_stat": lambda cls: recommended.get(cls, ["1", "1"])}

for name, extra in EXTRA.items():
    match = block(gf, name)
    def fix(row):
        values = row.group(2).split(";")
        if len(values) == 6:
            values += extra(row.group(1))
        return f"{row.group(1)}={{{';'.join(values)}}}"
    body = re.sub(r"(\w+)\s*=\s*\{([^}]*)\}", fix, match.group(1))
    gf = gf[:match.start(1)] + body + gf[match.end(1):]

# Olympiad rewards take two item/count pairs here; GF gives one. The second
# pair repeats the item with count 0, which adds nothing.
gf, bonus_count = re.subn(
    r"((?:classless|class|team)_bonus\s*=\s*\{)\s*(\[[^\]]+\])\s*=\s*(\d+)\s*\}",
    lambda m: f"{m.group(1)}{m.group(2)} = {m.group(3)};{m.group(2)} = 0}}", gf)

open(target, "w", encoding="utf-16", newline="").write(gf)
print("stat rows extended to eight values; recommended LUC/CHA from", new_path)
print(f"olympiad bonus rows given a second, empty pair: {bonus_count}")
