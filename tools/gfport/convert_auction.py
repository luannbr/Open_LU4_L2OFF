"""Converts the GF auctiondata.txt (item auction) to the format of this server.

usage: python convert_auction.py <GF auctiondata.txt> <item_pch.txt> <npcdata.txt> <output> <report>

GF gives each auction NPC a set of week days with one start time and
duration, and items with a registration weight. This server takes one
auction_date block per week day, numbers the items (index=) and sells them one
per auction in list order, so the weights are dropped. It refuses an auction
that ends after midnight (GF: 12:00 for 800 minutes); those end at 23:59.
nextinfo_open_time, which GF lacks, gets the 2018 value of 24 hours.

An auction whose NPC npcdata.txt does not define is left out: the GF Classic
pack defines none of its auction NPCs (32320-32322). With no auction left the
output is not written and the server runs without item auctions.
"""
import os
import re
import sys

source, item_pch, npcdata, target, report = sys.argv[1:6]
NEXTINFO_OPEN_HOURS = 24
DAY_END_SECONDS = 86399

items_known = set(re.findall(r"^\[([^\]]+)\]", open(item_pch, encoding="utf-16").read(), re.M))
npcs_known = {int(n) for n in re.findall(r"^npc_begin\s+\w+\s+(\d+)", open(npcdata, encoding="utf-16").read(), re.M)}
text = open(source, encoding="utf-16").read()

def field(block, name):
    match = re.search(rf"\b{name}\s*=\s*(\{{[^}}]*\}}|\S+)", block)
    return match.group(1) if match else None

rows, notes = [], []
for block in re.findall(r"^\s*auction_begin(.*?)^\s*auction_end", text, re.S | re.M):
    block = re.sub(r"//[^\n]*", "", block)
    npc = int(field(block, "npc_id"))
    if npc not in npcs_known:
        notes.append(f"NPC {npc}: not in npcdata; auction left out")
        continue
    doing = int(field(block, "auction_doing") or 1)
    days = [int(d) for d in field(block, "auction_week_day").strip("{}").split(";") if d.strip()]
    start = int(field(block, "auction_start_time"))
    duration = int(field(block, "auction_duration"))
    longest = (DAY_END_SECONDS - (start // 100 * 60 + start % 100) * 60) // 60
    if duration > longest:
        notes.append(f"NPC {npc}: auction_duration {duration} min ends after midnight; shortened to {longest}")
        duration = longest
    items = []
    for line in re.findall(r"item_begin(.*?)item_end", block):
        name = re.search(r"item\s*=\s*\[([^\]]+)\]", line).group(1)
        if name not in items_known:
            notes.append(f"NPC {npc}: item [{name}] does not exist, left out")
            continue
        items.append((name, int(field(line, "amount")), int(field(line, "price"))))
    rows += ["auction_begin", f"\tnpc_id={npc}", f"\tauction_doing={doing}",
             f"\tnextinfo_open_time={NEXTINFO_OPEN_HOURS}", f"\titem_count={len(items)}"]
    rows += [f"\tauction_date_begin\tauction_week_day={day}\tauction_start_time={start}\t"
             f"auction_duration={duration}\tauction_date_end" for day in days]
    rows += [f"item_begin\tindex={index}\titem=[{name}]\tamount={amount}\tprice={price}\titem_end"
             for index, (name, amount, price) in enumerate(items)]
    rows.append("auction_end")

if rows:
    open(target, "w", encoding="utf-16", newline="").write("\r\n".join(rows) + "\r\n")
elif os.path.exists(target):
    os.remove(target)
os.makedirs(os.path.dirname(report), exist_ok=True)
open(report, "w", encoding="utf-8", newline="\n").write("\n".join(notes) + ("\n" if notes else ""))
print(f"{rows.count('auction_begin')} auction NPCs, {sum(r.startswith('item_begin') for r in rows)} items"
      f"{'' if rows else ' (no file written)'}; {len(notes)} notes (see {os.path.basename(report)})")
