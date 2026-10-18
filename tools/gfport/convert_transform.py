"""Converts the GF transform.txt for this server.

usage: python convert_transform.py <GF transform.txt> <output>

This server expects luc_bonus and cha_bonus tables after men_bonus. GF has
no LUC or CHA, so both tables are zeros of the same length as men_bonus.
"""
import re
import sys

source, target = sys.argv[1], sys.argv[2]
lines = open(source, encoding="utf-16").read().split("\n")
out = []
added = 0
for index, line in enumerate(lines):
    out.append(line)
    match = re.match(r"(\s*)men_bonus\s*=\s*\{([^}]*)\}(\s*)$", line)
    if not match:
        continue
    following = "\n".join(lines[index + 1:index + 3])
    if "luc_bonus" in following:
        continue
    indent, values, tail = match.groups()
    zeros = ";".join("0" for _ in values.split(";"))
    out.append(f"{indent}luc_bonus = {{{zeros}}}{tail}")
    out.append(f"{indent}cha_bonus = {{{zeros}}}{tail}")
    added += 1

text = "\n".join(out)
# basic_stat lists eight stats here; LUC and CHA get 1 (their tables are zero).
text, extended = re.subn(r"(basic_stat\s*=\s*\{(?:-?[\d.]+;){5}-?[\d.]+)\}", r"\1;1;1}", text)

open(target, "w", encoding="utf-16", newline="").write(text)
print(f"luc_bonus/cha_bonus added to {added} stat blocks; basic_stat extended in {extended}")
