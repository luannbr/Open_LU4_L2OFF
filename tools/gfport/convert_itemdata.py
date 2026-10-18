"""Gives GF weapons, armor and accessories this server's item stats.

usage: python convert_itemdata.py <Itemdata.txt> [output, default the input]

GF items carry their stats as fields (physical_damage=5, attack_speed=379,
physical_defense=...). This server still parses those fields, but computes
stats only from validate_params={{pattack;5};{pattackspeed;379};...}, so a GF
weapon left as it is attacks at speed 0 and adds no P.Atk, and GF armor adds
no defense. Each weapon, armor and accessary record without validate_params
gets one built from its GF fields; the fields themselves stay.

GF has no is_npctrade either; this server reads a missing one as 0, so no
item could be sold to a merchant. GF sold to merchants what it could trade,
and the 2018 data agrees for nearly every item both have, so every record
without is_npctrade gets its is_trade value. Running it again changes nothing.
"""
import re
import sys

source = sys.argv[1]
target = sys.argv[2] if len(sys.argv) > 2 else source

# GF field -> validate_params name, in the order this server's own records use.
FIELDS = (
    ("physical_damage", "pattack"),
    ("critical", "pcritical"),
    ("hit_modify", "phit"),
    ("avoid_modify", "pavoid"),
    ("shield_defense", "shielddefense"),
    ("shield_defense_rate", "shielddefenserate"),
    ("attack_speed", "pattackspeed"),
    ("magical_damage", "mattack"),
    ("physical_defense", "pdefense"),
    ("magical_defense", "mdefense"),
)
KINDS = ("weapon", "armor", "accessary")

raw = open(source, "rb").read()
text = raw.decode("utf-16")
changed = 0
npc_trade = 0


def add_npc_trade(line):
    global npc_trade
    if not line.startswith("item_begin\t") or "\tis_npctrade=" in line:
        return line
    match = re.search(r"\tis_trade=(\d+)\t", line)
    if match is None:
        return line
    npc_trade += 1
    return re.sub(r"\titem_end", "\tis_npctrade=%s\titem_end" % match.group(1), line, count=1)


def convert(line):
    global changed
    line = add_npc_trade(line)
    parts = line.split("\t")
    if len(parts) < 3 or parts[0] != "item_begin" or parts[1] not in KINDS or "validate_params=" in line:
        return line
    values = []
    for field, name in FIELDS:
        match = re.search(r"\t" + field + r"=(-?[\d.]+)\t", line)
        if match is None:
            continue
        value = match.group(1)
        # Weapons list every stat, as this server's weapons do; other items only what they have.
        if parts[1] == "weapon" or float(value) != 0:
            values.append("{%s;%s}" % (name, value))
    if not values:
        return line
    changed += 1
    return re.sub(r"\titem_end", "\tvalidate_params={%s}\titem_end" % ";".join(values), line, count=1)


lines = text.split("\r\n")
text = "\r\n".join(convert(line) for line in lines)
open(target, "wb").write(text.encode("utf-16"))
print(f"  {target}: validate_params added to {changed} items, is_npctrade to {npc_trade}")
