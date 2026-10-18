"""Builds script_gf and html_gf: the GF Classic game data in the format of this server.

usage: python build_script_gf.py <GF script folder> [repo root] [output folder, default repo root]

Steps, in order:
1. Copy this server's script folder (without the AI and the Windows tools)
   so that files GF does not have keep their 2018 version.
2. Copy every GF data file over it.
3. Convert the GF files whose format changed after GF, write the GF
   experience table (gf_expdata.py), the stat bonuses (convert_pcparam.py)
   and the enchant costs and chances
   (convert_skillenchant.py), and fill what this engine needs and
   GF lacks: class categories CacheD requires, the 2018 Olympiad/clan hall/
   clan game zones with their areas, elevator doors, names for GF
   transformable areas, the GF clan skills in PledgeSkillInfo.xml
   (gen_pledgeskill.py). Spawn and trap lines naming NPCs the pack's
   npcdata.txt does not define are removed, and so are clan shop items GF
   lacks (port/ lists them). The item auction (convert_auction.py) and
   premium shop (convert_productdata.py) get this server's format.
4. Put back the 2018 files that describe the engine, not the content.
5. Remove 2018-only systems whose data refers to 2018 items, NPCs or skills.
6. Decompile the GF ai.obj to NASC source (ai/src/npc.ai and maker.ai),
   adapted to this server by gf_rules.py, and compile it with this server's
   NASC into ai.obj. ai/port/ holds the reports: handlers left out
   (errors.txt), changes that lose something (gf_rules.txt), the NASC log.
7. Build html_gf/html_USA and html_gf/html_KOREA: this server's pages with
   the GF pages (the html folder beside the GF script folder) over them.
   html_gf/port.txt lists what convert_html.py changed.
8. Copy the GF geodata into GeoData_gf (scripts/run-server.ps1 uses it for the
   gf data set).

The output folders are deleted and rebuilt each time, except GeoData_gf (unchanged
files are kept). NASC must be built
(build/windows-msvc/Release/NASC/NASC.exe).
"""
import os
import re
import shutil
import subprocess
import sys

gf = sys.argv[1]
root = sys.argv[2] if len(sys.argv) > 2 else os.getcwd()
# Where script_gf and html_gf go; another folder lets a running server keep its data.
target = sys.argv[3] if len(sys.argv) > 3 else root
base = os.path.join(root, "script")
out = os.path.join(target, "script_gf")
tools = os.path.dirname(os.path.abspath(__file__))

# Engine constants (parameter IDs, action IDs, visual effects): keep 2018.
KEEP_2018 = ["manual_pch.txt"]

# 2018-only systems. Their data refers to 2018 content that GF lacks, and the
# server starts without them. Each is listed with what is lost for now.
REMOVE = {
    "setitemdata.txt": "2018 armor sets (GF sets are in Itemdata.txt)",
    "shapeshiftedsetitemdata.txt": "2018 appearance sets",
    "combinationitemdata.txt": "item combination",
    "card_collect_system.txt": "card collection",
    "monsterbook.txt": "monster book",
    "monsterbook_trophy.txt": "monster book trophies",
    "ensouldata.txt": "soul crystal options (ensoul)",
    "Itemattrchange.txt": "attribute change",
    "artifact.txt": "artifacts",
    "enchantOption.txt": "artifact enchant options",
    "UserQuestData.txt": "2018 data-driven quests (GF quests come from the AI)",
    "eventdata.xml": "2018 events (Balthus and others; their rewards are 2018 items)",
    "questsystem.txt": "2018 data-driven quest steps (it also defines quests 258 and 464, which GF runs from the AI)",
    "luckygamedata.txt": "lucky game (2018 rewards)",
    "cardupdowngame.txt": "number card game (2018 NPC and rewards)",
    "FieldCycleMissionData.xml": "field cycle missions (2018 categories)",
    "skillsynergydata.txt": "2018 party synergy skills",
}

def run(script, *args):
    result = subprocess.run([sys.executable, "-I", "-X", "utf8", os.path.join(tools, script), *args],
                            capture_output=True, text=True, encoding="utf-8")
    if result.returncode != 0:
        sys.exit(f"{script} failed:\n{result.stderr}")
    print(f"  {script}: " + result.stdout.strip().replace("\n", "\n    "))

def find(folder, name):
    for entry in os.listdir(folder):
        if entry.lower() == name.lower():
            return os.path.join(folder, entry)
    return None

if os.path.isdir(out):
    shutil.rmtree(out)

print("1. 2018 base")
ignore = shutil.ignore_patterns("ai", "ai.obj", "ai.obj.*", "*.exe")
shutil.copytree(base, out, ignore=ignore)

print("2. GF data")
for entry in os.listdir(gf):
    source = os.path.join(gf, entry)
    if entry == "ai.obj" or not os.path.isfile(source):
        continue
    existing = find(out, entry)
    if existing:
        os.remove(existing)
    shutil.copy2(source, os.path.join(out, entry))

print("3. conversions")
run("convert_skilldata.py", os.path.join(gf, "skilldata.txt"), os.path.join(out, "skilldata.txt"))
run("convert_npcdata.py", os.path.join(gf, "npcdata.txt"), os.path.join(out, "npcdata.txt"))
run("convert_transform.py", os.path.join(gf, "transform.txt"), os.path.join(out, "transform.txt"))
# Weapon and armor stats: this server reads them from validate_params only.
run("convert_itemdata.py", find(out, "Itemdata.txt"))
# This server is configured as a TEST server and reads setting_test.txt.
run("convert_setting.py", os.path.join(gf, "setting.txt"), os.path.join(base, "setting_test.txt"),
    os.path.join(out, "setting_test.txt"))

# Stat bonus scale (GF percent, this server per mille) and the CHA/LUC tables GF lacks.
run("convert_pcparam.py", os.path.join(gf, "PC_parameter.txt"), find(base, "PC_parameter.txt"),
    find(out, "PC_parameter.txt"))

# GF keeps its experience table in L2Server.exe; this server reads expdata.txt.
run("gf_expdata.py", find(out, "expdata.txt"))

# Skill keys and the L2NPC skill summary use this server's packing.
run("gen_skill_pch.py", os.path.join(out, "skilldata.txt"), os.path.join(gf, "skill_pch2.txt"),
    os.path.join(base, "manual_pch.txt"), out)

# GF computes enchant SP and chances in L2Server.exe; this server reads them per record.
run("convert_skillenchant.py", os.path.join(gf, "skillenchantdata.txt"), os.path.join(out, "skilldata.txt"),
    find(out, "skillenchantdata.txt"), os.path.join(out, "port", "skillenchant.txt"))

# PremiumService: the grammar ends restartorigin only after restart_npc_item,
# which GF lacks. [none] is reported once and skipped.
path = find(out, "PremiumService.txt")
text = open(path, encoding="utf-16").read()
text, count = re.subn(r"(restart_item\s*=\s*\{[^}]*\})(\s*)(restartorigin_end)",
                      r"\1 restart_npc_item={[none]}\2\3", text)
open(path, "w", encoding="utf-16", newline="").write(text)
print(f"  PremiumService.txt: restart_npc_item added to {count} entries")

# Class categories that CacheD requires by name (Shared/src/CategoryDataContent.cpp)
# and GF lacks. They only list classes, so the 2018 definitions are added with new
# IDs after the GF ones; GF IDs stay as they are because ai.obj uses them.
REQUIRED_CATEGORIES = ["fifth_class_group", "sixth_class_group"] + [
    f"class_role_{role}_group" for role in
    ("knight", "warrior", "rogue", "archor", "wizard", "summoner", "enchanter", "support", "novice")]
block = r"category_define_begin\s*name\s*=\s*\[{}\].*?category_define_end"
base_data = open(find(base, "categorydata.txt"), encoding="utf-16", newline="").read()
pch_path, data_path = find(out, "category_pch.txt"), find(out, "categorydata.txt")
pch_text = open(pch_path, encoding="utf-16", newline="").read()
data_text = open(data_path, encoding="utf-16", newline="").read()
next_id = max(int(v) for v in re.findall(r"\]\s*=\s*(\d+)", pch_text)) + 1
added = []
for name in REQUIRED_CATEGORIES:
    if re.search(r"\[{}\]".format(name), pch_text):
        continue
    definition = re.search(block.format(name), base_data, re.S).group(0)
    pch_text = pch_text.rstrip("\r\n") + f"\r\n[{name}]\t=\t{next_id}\r\n"
    data_text = data_text.rstrip("\r\n") + "\r\n" + definition.replace("\r\n", "\n").replace("\n", "\r\n") + "\r\n"
    added.append(f"{name}={next_id}")
    next_id += 1
open(pch_path, "w", encoding="utf-16", newline="").write(pch_text)
open(data_path, "w", encoding="utf-16", newline="").write(data_text)
print(f"  categories added from 2018: {', '.join(added)}")

# Zone templates that only 2018 systems define: the Olympiad arenas
# (WOlympiadService uses types 147-149), provisional clan halls, clan games and
# the clan raid. GF has only instantzone_begin blocks. The 2018 blocks and the
# areas they list are added; the client and geodata are 2018, so the places exist.
def lines_of(path):
    return open(path, encoding="utf-16", newline="").read().splitlines()

def insert_before(lines, pattern, new_lines):
    """Inserts before the first line matching pattern (the grammar wants each kind of block together)."""
    index = next((i for i, line in enumerate(lines) if re.match(pattern, line)), len(lines))
    lines[index:index] = new_lines

zone_path, area_path = find(out, "instantzonedata.txt"), find(out, "areadata.txt")
zone_lines, area_lines = lines_of(zone_path), lines_of(area_path)
gf_zone_ids = {int(m.group(1)) for line in zone_lines for m in [re.match(r"\w+_begin\s+id=(\d+)", line)] if m}
gf_areas = {m.group(1) for line in area_lines for m in [re.match(r"area_begin\s+name=\[(\w+)\]", line)] if m}
base_areas = {m.group(1): line for line in lines_of(find(base, "areadata.txt"))
              for m in [re.match(r"area_begin\s+name=\[(\w+)\]", line)] if m}
added_zones, added_areas, new_zones = [], [], []
for line in lines_of(find(base, "instantzonedata.txt")):
    m = re.match(r"(\w+)_begin\s+id=(\d+)\s+name=(\w+)", line)
    if not m or m.group(1) == "instantzone" or int(m.group(2)) in gf_zone_ids:
        continue
    new_zones.append(line)
    added_zones.append(f"{m.group(3)}={m.group(2)}")
    for area in re.findall(r"\[(\w+)\]", re.search(r"area_list=\{([^}]*)\}", line).group(1)):
        if area not in gf_areas and area in base_areas:
            area_lines.append(base_areas[area])
            gf_areas.add(area)
            added_areas.append(area)
insert_before(zone_lines, r"\s*group_begin", new_zones)
open(zone_path, "w", encoding="utf-16", newline="").write("\r\n".join(zone_lines) + "\r\n")
open(area_path, "w", encoding="utf-16", newline="").write("\r\n".join(area_lines) + "\r\n")
print(f"  zones added from 2018: {', '.join(added_zones)}")
print(f"  areas added from 2018: {', '.join(added_areas)}")

# The pack's npcpos.txt and TrapData.txt name NPCs that its npcdata.txt does
# not define (2018 monsters, removed minigames). L2Server rejects each creation
# request and their makers retry forever, so those lines are removed.
npc_names = set(re.findall(r"^npc_begin\s+\w+\s+\d+\s+\[(\w+)\]",
                           open(find(out, "npcdata.txt"), encoding="utf-16").read(), re.M))
os.makedirs(os.path.join(out, "port"), exist_ok=True)
for name, pattern in (("npcpos.txt", r"\s*npc(?:_ex)?_begin\s+\[(\w+)\]"), ("TrapData.txt", r"\s*trap_begin\s+\[(\w+)\]")):
    path = find(out, name)
    kept, removed = [], {}
    maker_start, maker_npcs, empty_makers = None, 0, 0
    for line in open(path, encoding="utf-16", newline="").read().split("\n"):
        m = re.match(pattern, line)
        if m and m.group(1) not in npc_names:
            removed[m.group(1)] = removed.get(m.group(1), 0) + 1
            continue
        if re.match(r"\s*npcmaker(_ex)?_begin", line):
            maker_start, maker_npcs = len(kept), 0
        elif m:
            maker_npcs += 1
        kept.append(line)
        # The grammar needs at least one NPC per maker; drop makers left empty.
        if re.match(r"\s*npcmaker(_ex)?_end", line) and maker_start is not None:
            if maker_npcs == 0:
                del kept[maker_start:]
                empty_makers += 1
            maker_start = None
    open(path, "w", encoding="utf-16", newline="").write("\n".join(kept))
    if empty_makers:
        print(f"  {name}: {empty_makers} makers left without NPCs removed")
    report = os.path.splitext(name)[0] + "_removed.txt"
    open(os.path.join(out, "port", report), "w", encoding="utf-8").write(
        "".join(f"{npc}\t{count}\n" for npc, count in sorted(removed.items())))
    print(f"  {name}: {sum(removed.values())} lines naming {len(removed)} NPCs missing from npcdata removed "
          f"(port/{report})")

# RestrictAreaData.txt: this server's grammar requires a name in every block;
# GF transformable blocks have none, and one parse error drops every restricted
# area (no-fly, no-bookmark, ...). Give each one a name.
path = find(out, "RestrictAreaData.txt")
text = open(path, encoding="utf-16", newline="").read()
counter = iter(range(1, 100000))
text, count = re.subn(r"(?m)^(\s*transformable_begin)(\s+)(?!name\s*=)",
                      lambda m: f"{m.group(1)}\tname=[gf_transformable_{next(counter):02d}]{m.group(2)}", text)
open(path, "w", encoding="utf-16", newline="").write(text)
print(f"  RestrictAreaData.txt: names given to {count} transformable areas")

# elevatordata.txt is 2018-only (CShuttleManager); its outer doors exist in the
# 2018 client and doordata, not in the GF doordata.
door_path = find(out, "doordata.txt")
door_lines = lines_of(door_path)
gf_doors = {m.group(1) for line in door_lines for m in [re.match(r"\s*door_begin\s+\[(\w+)\]", line)] if m}
base_doors = {m.group(1): line for line in lines_of(find(base, "doordata.txt"))
              for m in [re.match(r"\s*door_begin\s+\[(\w+)\]", line)] if m}
wanted = re.findall(r"outer_door_name\s*=\s*\[(\w+)\]", open(find(out, "elevatordata.txt"), encoding="utf-16").read())
added_doors = [door for door in dict.fromkeys(wanted) if door not in gf_doors and door in base_doors]
insert_before(door_lines, r"\s*(signboard|controltower)_begin", [base_doors[door] for door in added_doors])
open(door_path, "w", encoding="utf-16", newline="").write("\r\n".join(door_lines) + "\r\n")
print(f"  doors added from 2018 for elevators: {', '.join(added_doors)}")

# Clan skills: the 2018 PledgeSkillInfo.xml names 2018 skills; build it from the GF ones.
run("gen_pledgeskill.py", os.path.join(gf, "skillacquire.txt"), os.path.join(out, "skilldata.txt"),
    find(out, "PledgeSkillInfo.xml"), os.path.join(out, "port", "pledgeskill.txt"))

# The 2018 clan shop (PledgeItemInfo.xml) sells 2018 items; keep those GF has.
item_names = set(re.findall(r"^\[(\w+)\]", open(find(out, "item_pch.txt"), encoding="utf-16").read(), re.M))
path = find(out, "PledgeItemInfo.xml")
text = open(path, encoding="utf-16", newline="").read()
dropped = []
def keep_item(match):
    if match.group(2) in item_names:
        return match.group(0)
    dropped.append(match.group(2))
    return ""
text = re.sub(r'(\r?\n)?[ \t]*<Item class_id="(\w+)".*?</Item>', keep_item, text, flags=re.S)
open(path, "w", encoding="utf-16", newline="").write(text)
print(f"  PledgeItemInfo.xml: {len(dropped)} clan shop items missing from GF removed: {', '.join(dropped)}")

# Item auction and premium shop. The auction file is left out when none of its NPCs exist.
run("convert_auction.py", os.path.join(gf, "auctiondata.txt"), find(out, "item_pch.txt"), find(out, "npcdata.txt"),
    find(out, "auctiondata.txt"), os.path.join(out, "port", "auction.txt"))
# Product IDs must be ones the client knows; client_products.txt comes from its ProductName-eu.dat.
run("convert_productdata.py", os.path.join(gf, "productdata.txt"), find(out, "item_pch.txt"),
    os.path.join(tools, "client_products.txt"), find(out, "productdata.txt"), os.path.join(out, "port", "productdata.txt"))

print("4. 2018 engine files")
for name in KEEP_2018:
    shutil.copy2(find(base, name), os.path.join(out, name))
    print(f"  {name}")

print("5. 2018-only systems removed")
for name, what in REMOVE.items():
    path = find(out, name)
    if path:
        os.remove(path)
        print(f"  {name}: {what}")

print("6. AI")
ai_src = os.path.join(out, "ai", "src")
report = os.path.join(out, "ai", "port")
os.makedirs(ai_src)
run("decompile_ai.py", os.path.join(gf, "ai.obj"), report, "--items", os.path.join(gf, "item_pch.txt"), "--gf", gf)
for name in ("npc.ai", "maker.ai"):
    shutil.move(os.path.join(report, name), os.path.join(ai_src, name))
nasc = os.path.join(root, "build", "windows-msvc", "Release", "NASC", "NASC.exe")
nasc_out = os.path.join(report, "nasc_out")
result = subprocess.run([nasc, "--root", out, "--output", nasc_out, "--module", "npc.ai", "--module", "maker.ai"],
                        capture_output=True, text=True, encoding="utf-8", errors="replace")
open(os.path.join(report, "nasc.log"), "w", encoding="utf-8").write(result.stdout + result.stderr)
if result.returncode != 0:
    sys.exit(f"NASC failed, see {os.path.join(report, 'nasc.log')}")
shutil.move(os.path.join(nasc_out, "ai.obj"), os.path.join(out, "ai.obj"))
shutil.rmtree(nasc_out)
print("  ai.obj compiled; reports in ai/port")

print("7. HTML")
html_out = os.path.join(target, "html_gf")
if os.path.isdir(html_out):
    shutil.rmtree(html_out)
os.makedirs(html_out)
gf_html = os.path.join(os.path.dirname(os.path.abspath(gf)), "html")
for catalog in ("html_USA", "html_KOREA"):
    run("convert_html.py", gf_html, os.path.join(root, catalog), os.path.join(html_out, catalog),
        os.path.join(html_out, "port.txt"))

print("8. Geodata")
# GF geodata (the geodata folder beside the GF script folder): the zones its
# geo_index.txt lists, the NPC path network and the SuperPoint image that goes
# with the GF superpointinfo.txt. Only the first geo_index section is the zone
# list. Files already copied (same size and time) are kept; 1.7 GB otherwise.
geo_source = os.path.join(os.path.dirname(os.path.abspath(gf)), "geodata")
geo_out = os.path.join(target, "GeoData_gf")
os.makedirs(geo_out, exist_ok=True)
index_words = open(os.path.join(geo_source, "geo_index.txt"), encoding="ascii").read().split()
zones = index_words[1:1 + int(index_words[0])]
copied = 0
for name in ["geo_index.txt", "pathnode.bin", "pathnode.idx", "SuperPoint.bin"] + zones:
    source, destination = os.path.join(geo_source, name), os.path.join(geo_out, name)
    stat = os.stat(source)
    if os.path.exists(destination):
        existing = os.stat(destination)
        if existing.st_size == stat.st_size and int(existing.st_mtime) == int(stat.st_mtime):
            continue
    shutil.copy2(source, destination)
    copied += 1
print(f"  GeoData_gf: {len(zones)} zones from geo_index.txt; {copied} files copied")
