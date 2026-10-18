"""Builds an HTML catalog for the GF data: this server's catalog with every GF
page over it.

usage: python convert_html.py <GF html folder> <2018 catalog, e.g. html_USA> <output folder> [report]

Pages GF does not have keep their 2018 version, because L2Server and 2018
data still ask for some of them. GF pages are written as UTF-16LE with BOM,
which is what L2Server reads; a few GF pages are plain ASCII or end with a
stray byte. Bypass commands that this server spells differently are renamed.
Subfolders of the GF html folder (the extender's admin pages) are not used.
"""
import os
import shutil
import sys

gf, base, out = sys.argv[1:4]
report_path = sys.argv[4] if len(sys.argv) > 4 else None

# GF bypass command -> this server's command (UserSocketHTMLCmd.cpp, Auction.cpp).
BYPASS = {
    "bypass -h item_auction_next_info": "bypass -h item_auction_nextinfo",
}
# GF commands this server does not have; the pages are copied, the link does nothing.
UNSUPPORTED = ["refresh_fishing"]


def decode(data):
    if data[:2] == b"\xff\xfe":
        data = data[2:]
        if len(data) % 2:
            data = data[:-1]
        return data.decode("utf-16-le", "replace").rstrip("\0")
    if data[:3] == b"\xef\xbb\xbf":
        data = data[3:]
    return data.decode("utf-8", "replace").strip("\0")


if os.path.isdir(out):
    shutil.rmtree(out)
shutil.copytree(base, out)
existing = {name.lower(): name for name in os.listdir(out)}

lines = []
replaced = added = fixed = 0
renamed = {}
for name in sorted(os.listdir(gf)):
    source = os.path.join(gf, name)
    if not os.path.isfile(source) or not name.lower().endswith((".htm", ".html")):
        continue
    data = open(source, "rb").read()
    text = decode(data)
    if data[:2] != b"\xff\xfe" or (len(data) - 2) % 2 or data.endswith(b"\0\0"):
        fixed += 1
        lines.append(f"encoding fixed: {name}")
    for old, new in BYPASS.items():
        if old in text:
            text = text.replace(old, new)
            renamed[old] = renamed.get(old, 0) + 1
            lines.append(f"bypass renamed: {name}: {old} -> {new}")
    for command in UNSUPPORTED:
        if command in text:
            lines.append(f"bypass not supported here: {name}: {command}")
    old_name = existing.pop(name.lower(), None)
    if old_name:
        os.remove(os.path.join(out, old_name))
        replaced += 1
    else:
        added += 1
    with open(os.path.join(out, name), "wb") as f:
        f.write(b"\xff\xfe" + text.encode("utf-16-le"))

skipped = [entry for entry in os.listdir(gf) if os.path.isdir(os.path.join(gf, entry))]
lines.append("GF subfolders not used: " + ", ".join(skipped))
if report_path:
    open(report_path, "w", encoding="utf-8").write("\n".join(lines) + "\n")
print(f"{replaced} pages replaced, {added} added, {len(existing)} kept from 2018, "
      f"{fixed} encodings fixed, bypass renamed {sum(renamed.values())}")
