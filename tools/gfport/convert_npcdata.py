"""Converts the GF npcdata.txt for this server.

usage: python convert_npcdata.py <GF npcdata.txt> <output>

- Every NPC gets an empty vitality_item_drop_list; this server reports each
  NPC without one.
- item_make_list (always empty in GF) is not known here and is removed.
"""
import re
import sys

source, target = sys.argv[1], sys.argv[2]
text = open(source, encoding="utf-16").read()

text, removed = re.subn(r"\titem_make_list=\{\}", "", text)
text, added = re.subn(r"(\t)(npc_end)", r"\1vitality_item_drop_list={}\t\2", text)

open(target, "w", encoding="utf-16", newline="").write(text)
print(f"empty item_make_list removed: {removed}; vitality_item_drop_list added: {added}")
