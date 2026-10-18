"""Converts the GF PC_parameter.txt for this server.

usage: python convert_pcparam.py <GF PC_parameter.txt> <2018 PC_parameter.txt> <output>

Stat bonuses: GF stores a bonus as a percentage, (v + 100) / 100 (GF L2Server
CPCParamDB setter); this server reads (v + 1000) / 1000. The GF values are
multiplied by 10. GF lists STR..WIT 1 to 99 and this server 1 to 200; values
above 99 keep the value of 99.

Tables GF lacks:
  cha_bonus: this server multiplies HP, MP, CP, attack and defence by the
             square root of the CHA bonus. Without the table the bonus is 0
             and every character has 1 HP. GF has no CHA: all values are 0,
             a bonus of 1.
  luc_bonus, pc_standard_exp_by_level: only 2018 luck features use them;
             the 2018 tables are copied.
"""
import re
import sys

gf_path, base_path, target = sys.argv[1:4]
text = open(gf_path, encoding="utf-16", newline="").read()
base = open(base_path, encoding="utf-16", newline="").read()
newline = "\r\n" if "\r\n" in text else "\n"

STATS = ["str", "int", "con", "men", "dex", "wit"]
MAX_STAT = 200

def section(source, name):
    return re.search(rf"^{name}_begin[^\n]*\n.*?^{name}_end[^\n]*\n?", source, re.S | re.M)

for stat in STATS:
    match = section(text, f"{stat}_bonus")
    values = {int(k): int(v) for k, v in re.findall(r"attribute_(\d+)\s*=\s*(-?\d+)", match.group(0))}
    last = max(values)
    rows = [f"{stat}_bonus_begin"]
    for attribute in range(1, MAX_STAT + 1):
        rows.append(f"\tattribute_{attribute} = {values[min(attribute, last)] * 10}")
    rows.append(f"{stat}_bonus_end")
    text = text[:match.start()] + newline.join(rows) + newline + text[match.end():]

def copied(name):
    block = section(base, name).group(0)
    return block.replace("\r\n", "\n").replace("\n", newline).rstrip(newline) + newline

added = []
wit_end = section(text, "wit_bonus").end()
extra = ""
if not section(text, "luc_bonus"):
    extra += copied("luc_bonus")
    added.append("luc_bonus (2018)")
if not section(text, "cha_bonus"):
    rows = ["cha_bonus_begin"] + [f"\tattribute_{a} = 0" for a in range(1, MAX_STAT + 1)] + ["cha_bonus_end"]
    extra += newline.join(rows) + newline
    added.append("cha_bonus (neutral)")
text = text[:wit_end] + extra + text[wit_end:]
if not section(text, "pc_standard_exp_by_level"):
    text = text.rstrip("\r\n") + newline + newline + copied("pc_standard_exp_by_level")
    added.append("pc_standard_exp_by_level (2018)")

open(target, "w", encoding="utf-16", newline="").write(text)
print(f"stat bonuses x10 and extended to {MAX_STAT}; added {', '.join(added)}")
