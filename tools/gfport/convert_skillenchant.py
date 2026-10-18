"""Converts the GF skillenchantdata.txt to the format of this server.

usage: python convert_skillenchant.py <GF skillenchantdata.txt> <converted skilldata.txt> <output> <report>

GF lists only the codex each enchant type takes. Its L2Server.exe computes the
rest (SkillEnchantOperator in the GF Classic binary):
  base cost  = importance * 0.01 * D * max(1, 536 - 6M) * 0.01 / 3, truncated;
               * 1.2 when M = 80; then rounded down to 10000 and * 0.1.
               M is the magic level of the enchanted skill level and D the
               experience of level M (GF table, gf_expdata.py).
  SP         = base cost * 1 (normal), 5 (safe), 0.8 (untrain), 0.2 (route change).
  EXP        = the same amounts. This server has no EXP cost; they are dropped.
  chance     = max(1, min(99, B + 10 * (user level - M)) - 2 * enchant step),
               B = 84 when user level >= M, else 70. Untrain and route change: 100.
This server keeps one chance per record, so it is taken at user level = M.
The fifth type (force, a 2018 pass item) does not exist in GF: it gets an SP
cost no character can have, so the server refuses it.

GF level 100 * route + step is this server's sub_level 1000 * route + step,
as convert_skilldata.py writes it.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gf_expdata import GF_EXP

source, skilldata, target, report = sys.argv[1:5]

FORCE_BLOCKED_SP = 2**63 - 1
TYPE_SP_RATE = (1.0, 5.0, 0.8, 0.2)  # normal, safe, untrain, route change

def level_exp(level):
    return GF_EXP[level] - GF_EXP[level - 1]

def base_cost(importance, magic_level):
    value = importance * 0.01
    value *= level_exp(magic_level)
    value *= max(1, 536 - 6 * magic_level)
    value *= 0.01
    value /= 3.0
    value = int(value)
    if magic_level == 80:
        value = int(value * 1.2)
    value = int(value * 0.0001) * 10000
    return min(int(value * 0.1), 2**31 - 1)  # GF keeps SP in an int

def chance(step):
    return max(1, 84 - 2 * step)

# Converted skilldata: name -> (id, level, sub_level) and the magic level of each.
skills, magic = {}, {}
entry = re.compile(r"skill_name\s*=\s*\[([^\]]+)\].*?skill_id\s*=\s*(\d+)\s+level\s*=\s*(\d+)"
                   r"(?:\s+sub_level\s*=\s*(\d+))?.*?magic_level\s*=\s*(-?\d+)")
for line in open(skilldata, encoding="utf-16"):
    match = entry.search(line)
    if match:
        key = (int(match.group(2)), int(match.group(3)), int(match.group(4) or 0))
        skills[match.group(1)] = key
        magic[key] = int(match.group(5))

record = re.compile(r"original_skill\s*=\s*\[([^\]]+)\]\s+route_id\s*=\s*(\d+)\s+enchant_id\s*=\s*(\d+)\s+"
                    r"skill_level\s*=\s*(\d+)\s+importance\s*=\s*(\d+)\s+required_item\s*=\s*\{(.*)\}\s+enchant_skill_end")
item = re.compile(r"\{\[([^\]]+)\];(\d+)\}")

rows, problems, renumbered = [], [], set()
for number, line in enumerate(open(source, encoding="utf-16"), 1):
    line = line.strip()
    if not line:
        continue
    match = record.search(line)
    if not match:
        problems.append(f"line {number}: not understood: {line[:120]}")
        continue
    name, gf_route, step, gf_level, importance = match.group(1), *map(int, match.group(2, 3, 4, 5))
    items = item.findall(match.group(6))
    if len(items) != 4 or gf_level % 100 != step:
        problems.append(f"line {number}: unexpected record: {line[:120]}")
        continue
    # GF enchants by skill level; route_id is not used. A few traps say route 1
    # at levels 201+, so the route comes from the level, like the sub_level.
    route = gf_level // 100
    if route != gf_route:
        renumbered.add(name)
    original = skills.get(name)
    if original is None:
        problems.append(f"line {number}: skill [{name}] is not in skilldata")
        continue
    sub_level = route * 1000 + step
    enchanted = (original[0], original[1], sub_level)
    if enchanted not in magic:
        problems.append(f"line {number}: [{name}] has no level {original[1]} sub_level {sub_level} in skilldata")
        continue
    cost = base_cost(importance, magic[enchanted])
    sp = [int(cost * rate) for rate in TYPE_SP_RATE] + [FORCE_BLOCKED_SP]
    prob = [chance(step), chance(step), 100, 100, 0]
    groups = ";".join("{{[%s];%s}}" % pair for pair in items) + ";{}"
    rows.append(f"enchant_skill_begin\toriginal_skill=[{name}]\troute_id={route}\tenchant_id={step}\t"
                f"skill_sub_level={sub_level}\timportance={importance}\t"
                f"required_sp={{{';'.join(map(str, sp))}}}\tenchant_prob={{{';'.join(map(str, prob))}}}\t"
                f"required_item={{{groups}}}\tenchant_skill_end")

if renumbered:
    problems.append(f"route_id taken from skill_level (GF route_id differs): {', '.join(sorted(renumbered))}")
open(target, "w", encoding="utf-16", newline="").write("\r\n".join(rows) + "\r\n")
os.makedirs(os.path.dirname(report), exist_ok=True)
open(report, "w", encoding="utf-8", newline="\n").write("\n".join(problems) + ("\n" if problems else ""))
print(f"{len(rows)} enchant records; {len(problems) - bool(renumbered)} skipped, "
      f"{len(renumbered)} skills renumbered (see {os.path.basename(report)})")
