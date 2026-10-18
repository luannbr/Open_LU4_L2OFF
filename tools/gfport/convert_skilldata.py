"""Converts the GF skilldata.txt to the effect syntax of this server.

usage: python convert_skilldata.py <GF skilldata.txt> <output skilldata.txt>

Effects whose parameter list grew after GF get the value that keeps the GF
behaviour. Effects that only the GF Classic extender (L2Server.dll) knew are
removed and listed. Enchanted levels (GF level 100*route+step) become the
highest normal level plus sub_level 1000*route+step.
"""
import collections
import re
import sys

source, target = sys.argv[1], sys.argv[2]
text = open(source, encoding="utf-16").read()

# name -> (GF argument count, function returning the new argument list)
def append(*values):
    return lambda args: args + list(values)

def prepend(*values):
    return lambda args: list(values) + args

RULES = {
    # Instant damage: this server adds physicalProb at the end.
    "i_p_attack": (3, append("0")),
    "i_p_soul_attack": (3, append("0")),
    "i_physical_attack_hp_link": (3, append("0")),
    "i_p_attack_by_dist": (3, append("0")),
    "i_energy_attack": (4, append("0")),
    "i_enchant_attribute": (3, append("0", "0")),
    # Over-time HP/MP: GF values are flat amounts.
    "t_hp": (2, append("diff")),
    "t_mp": (2, append("diff")),
    "t_hp_fatal": (2, append("diff")),
    "cub_hp": (2, append("diff")),
    # GF had no probability here; 5 is what this server uses for the same skills.
    "i_steal_abnormal": (2, append("5")),
    "op_check_abnormal": (2, append("1")),
    "op_enchant_range": (2, append("none")),
    "p_hate_attack": (2, append("per")),
    "p_disarm": (0, append("slot_rhand")),
    # A weapon list was added in front; GF applied them to every weapon.
    "p_attack_trait": (2, prepend("{all}")),
    "p_hit_number": (2, prepend("{all}")),
    "p_skill_critical": (1, prepend("{all}")),
    # Conditions: GF checked the caster; this server names the target.
    "remain_hp_per": (2, prepend("oct_caster")),
    "target_my_pledge": (0, append("include_me")),
    # GF names one trigger skill ID; this server takes a list of IDs.
    "p_trigger_skill_by_skill": (4, lambda args: ["{" + args[0] + "}"] + args[1:]),
    # HP, MP, then CP (same as HP) and a skill cast on revival (none in GF).
    "p_resurrection_special": (2, lambda args: args + [args[0], "[none]"]),
}
# GF Classic extender effects with no counterpart in this server.
REMOVED = {"p_2h_sword_bonus", "p_2h_blunt_bonus", "p_recovery_vp", "i_vp_up"}

ITEM = re.compile(r"\{([a-z][a-z0-9_]*)((?:;(?:\{[^{}]*\}|[^{};]*))*)\}")
ARG = re.compile(r";(\{[^{}]*\}|[^{};]*)")

changed = collections.Counter()
removed = collections.defaultdict(list)
unexpected = collections.Counter()

def convert_line(line):
    skill = re.search(r"skill_name\s*=\s*\[([^\]]+)\]", line)
    skill_name = skill.group(1) if skill else "?"

    def replace(match):
        name, raw = match.group(1), match.group(2)
        if name in REMOVED:
            removed[name].append(skill_name)
            return "\0"
        rule = RULES.get(name)
        if rule is None:
            return match.group(0)
        args = ARG.findall(raw)
        count, build = rule
        if len(args) != count:
            unexpected[(name, len(args))] += 1
            return match.group(0)
        changed[name] += 1
        return "{" + ";".join([name] + build(args)) + "}"

    line = ITEM.sub(replace, line)
    # GF names a single visual effect; this server takes a list.
    line = re.sub(r"(abnormal_visual_effect\s*=\s*)([a-z_][a-z0-9_]*)", lambda m: m.group(1) + "{" + m.group(2) + "}", line)
    # Drop the separators left by removed effects: {a;\0} {\0;a} {\0}
    line = re.sub(r";\0|\0;|\0", "", line)
    # This server reports every skill without change_skill_id; 0 means none.
    if "skill_begin" in line and "change_skill_id" not in line:
        line = re.sub(r"\tskill_end", "\tchange_skill_id = 0\tskill_end", line, count=1)
    return line

LEVEL = re.compile(r"(skill_id\s*=\s*(\d+)\s+level\s*=\s*)(\d+)")

# GF stores enchant route r, step n as level 100*r+n. This server keeps the
# highest normal level and puts the enchant in sub_level 1000*r+n.
max_base_level = collections.defaultdict(int)
for skill_id, level in (m.groups()[1:] for m in LEVEL.finditer(text)):
    if int(level) < 100:
        max_base_level[skill_id] = max(max_base_level[skill_id], int(level))
enchant_entries = 0

def convert_enchant_level(line):
    global enchant_entries
    match = LEVEL.search(line)
    if not match or int(match.group(3)) < 100:
        return line
    level = int(match.group(3))
    route, step = divmod(level, 100)
    base = max_base_level[match.group(2)]
    enchant_entries += 1
    return (line[:match.start(3)] + f"{base}\tsub_level = {route * 1000 + step}" + line[match.end(3):])

lines = [convert_enchant_level(convert_line(line)) for line in text.split("\n")]
open(target, "w", encoding="utf-16", newline="").write("\n".join(lines))

print(f"enchant levels moved to sub_level: {enchant_entries}")
print("converted effects:")
for name, count in sorted(changed.items()):
    print(f"  {name}: {count}")
print("removed extender effects:")
for name, skills in sorted(removed.items()):
    print(f"  {name}: {len(skills)} ({', '.join(sorted(set(skills))[:6])})")
if unexpected:
    print("left unchanged, unexpected argument count:")
    for (name, count), n in sorted(unexpected.items()):
        print(f"  {name} with {count}: {n}")
