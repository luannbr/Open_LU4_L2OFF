"""Writes skill_pch.txt and skill_pch2.txt for the converted GF skills.

usage: python gen_skill_pch.py <converted skilldata.txt> <GF skill_pch2.txt>
                              <2018 manual_pch.txt> <output folder>

skill_pch.txt: GF keys were id*65536+level; this server packs
(id << 32) | (level << 16) | sub_level. Recomputed from the converted skilldata.

skill_pch2.txt (L2NPC skill summary): the GF rows with the new key, the
abnormal type renumbered through its name (the ab_* constants changed), and
an is_hate column (0) that GF did not have before the attribute.
"""
import collections
import re
import sys

skilldata, gf_pch2, new_manual, out_dir = sys.argv[1:5]

def u16(path):
    return open(path, encoding="utf-16", errors="replace").read()

def key(skill_id, level, sub_level):
    return (skill_id << 32) | (level << 16) | sub_level

# skill_pch.txt from the converted skilldata.
entry = re.compile(r"skill_name\s*=\s*\[([^\]]+)\].*?skill_id\s*=\s*(\d+)\s+level\s*=\s*(\d+)(?:\s+sub_level\s*=\s*(\d+))?")
rows = []
max_base = collections.defaultdict(int)
abnormal_name = {}
for line in u16(skilldata).split("\n"):
    match = entry.search(line)
    if not match:
        continue
    name, skill_id, level, sub_level = match.group(1), int(match.group(2)), int(match.group(3)), int(match.group(4) or 0)
    rows.append(f"[{name}]\t=\t{key(skill_id, level, sub_level)}")
    abnormal = re.search(r"abnormal_type\s*=\s*(\w+)", line)
    abnormal_name[key(skill_id, level, sub_level)] = abnormal.group(1) if abnormal else "none"
    if sub_level == 0:
        max_base[skill_id] = max(max_base[skill_id], level)
with open(f"{out_dir}/skill_pch.txt", "w", encoding="utf-16", newline="") as f:
    f.write("\r\n".join(rows) + "\r\n")

# Abnormal type numbers changed with the ab_* constants. The skill's
# abnormal_type name in skilldata gives the number this server uses.
new_ab = {k: int(v) for k, v in re.findall(r"\[(ab_[^\]]+)\]\s*=?\s*(-?\d+)", u16(new_manual))}

pch2 = []
unknown_abnormal = collections.Counter()
for line in u16(gf_pch2).split("\n"):
    parts = line.split()
    if len(parts) != 13:
        continue
    gf_key, fields = int(parts[0]), parts[1:]
    skill_id, level = gf_key >> 16, gf_key & 0xFFFF
    if level >= 100:  # enchant: route*100+step -> highest level, sub_level route*1000+step
        route, step = divmod(level, 100)
        new_key = key(skill_id, max_base[skill_id], route * 1000 + step)
    else:
        new_key = key(skill_id, level, 0)
    name = abnormal_name.get(new_key, "none")
    if name == "none":
        abnormal = -1
    elif "ab_" + name in new_ab:
        abnormal = new_ab["ab_" + name]
    else:
        unknown_abnormal[name] += 1
        abnormal = -1
    fields[5] = str(abnormal)
    fields = fields[:10] + ["0"] + fields[10:]  # is_hate before the attribute
    pch2.append(" ".join([str(new_key)] + fields))
with open(f"{out_dir}/skill_pch2.txt", "w", encoding="utf-16", newline="") as f:
    f.write("\r\n".join(pch2) + "\r\n")

print(f"skill_pch.txt: {len(rows)} skills; skill_pch2.txt: {len(pch2)} rows")
if unknown_abnormal:
    print("abnormal types unknown here (set to none):", dict(unknown_abnormal))
