"""Converts the GF productdata.txt (premium shop) to the format of this server.

usage: python convert_productdata.py <GF productdata.txt> <item_pch.txt> <client_products.txt> <output> <report>

GF lists every product once per region (location_id 4, 5, 7, 8). This server
loads only the products whose location_id equals Country in l2server.ini (0
here), so the region with the most products (8) is kept with location_id 0.
Prices are game points: GF price=N becomes price={0;N} (payment type 0).
The GF flags get this server's names: is_event_product -> is_advertising,
is_best_product -> is_recommend, is_new_product -> is_popular.

The client shows only products whose ID its ProductName-eu.dat lists, and none
of the GF IDs are there. It takes the name and icon from the items the server
sends and the description from that file, so each GF product gets, in order, a
client ID without description (client_products.txt, from client_products.py).
The report lists the GF ID behind each new ID.
"""
import collections
import os
import re
import sys

source, item_pch, client_list, target, report = sys.argv[1:6]
COUNTRY = 0

items_known = set(re.findall(r"^\[([^\]]+)\]", open(item_pch, encoding="utf-16").read(), re.M))
free_ids = [int(line.split("\t")[0]) for line in open(client_list, encoding="utf-8")
            if line.rstrip("\n").split("\t")[1] == ""]
lines = [line for line in open(source, encoding="utf-16").read().splitlines() if "product_begin" in line]
regions = collections.Counter(re.search(r"location_id=(\d+)", line).group(1) for line in lines)
region = regions.most_common(1)[0][0]

FLAGS = {"is_event_product": "is_advertising", "is_best_product": "is_recommend", "is_new_product": "is_popular"}

rows, notes, ids = [], [], []
for line in lines:
    if re.search(r"location_id=(\d+)", line).group(1) != region:
        continue
    product = re.search(r"\bid=(\d+)", line).group(1)
    names = re.findall(r"\{\[([^\]]+)\];\d+\}", line)
    missing = [name for name in names if name not in items_known]
    if missing:
        notes.append(f"product {product}: items {', '.join(missing)} do not exist; product left out")
        continue
    if len(rows) == len(free_ids):
        notes.append(f"product {product}: no client product ID left; product left out")
        continue
    client_id = free_ids[len(rows)]
    ids.append(f"{client_id}\tGF {product}\t{', '.join(names)}")
    line = re.sub(r"\bid=\d+", f"id={client_id}", line, count=1)
    line = re.sub(r"\bprice=(\d+)", r"price={0;\1}", line)
    line = re.sub(r"location_id=\d+", f"location_id={COUNTRY}", line)
    for old, new in FLAGS.items():
        line = re.sub(rf"\b{old}=", f"{new}=", line)
    rows.append(line)

open(target, "w", encoding="utf-16", newline="").write("\r\n".join(rows) + "\r\n")
os.makedirs(os.path.dirname(report), exist_ok=True)
open(report, "w", encoding="utf-8", newline="\n").write("\n".join(notes + ids) + "\n")
print(f"{len(rows)} products from region {region} (of {dict(regions)}) on client IDs; {len(notes)} left out "
      f"(see {os.path.basename(report)})")
