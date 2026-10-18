"""Writes the NPC tables for the LU4 port: L2Server/etc/lu4_npcname.txt and
L2Server/etc/lu4_npcstring.txt.

usage: python lu4_npc_tables.py <client system dir> <output dir>

The server knows NPCs only by their script names and sends NPC lines as
NpcString ids, but the LU4 client shows a speaker's name only on an ordinary
chat line, which carries the name and the text. L2Server builds that line
from these tables. The inputs are the 4game EU client's NpcName-eu.dat and
NpcString-eu.dat (Lineage2Ver413, l2encdec key, see
../gfport/client_products.py). NpcName record: d ID, name, nickname (Unreal
compact-index strings), d colour; NpcString record: d ID, text. Each output
has one line per non-empty entry: ID, tab, text in UTF-8, with backslash,
newline and tab escaped as \\, \n and \t.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "gfport"))
from client_products import decode  # noqa: E402


class Records:
    def __init__(self, path):
        self.raw = decode(path)
        self.position = 4
        self.count = struct.unpack_from("<I", self.raw, 0)[0]

    def int32(self):
        value = struct.unpack_from("<I", self.raw, self.position)[0]
        self.position += 4
        return value

    def compact(self):
        byte = self.raw[self.position]; self.position += 1
        value, negative = byte & 0x3F, byte & 0x80
        if byte & 0x40:
            shift = 6
            while True:
                byte = self.raw[self.position]; self.position += 1
                value |= (byte & 0x7F) << shift
                shift += 7
                if not byte & 0x80:
                    break
        return -value if negative else value

    def text(self):
        length = self.compact()
        if length >= 0:
            value = self.raw[self.position:self.position + length].rstrip(b"\0").decode("latin-1")
            self.position += length
        else:
            value = self.raw[self.position:self.position - 2 * length].decode("utf-16le").rstrip("\0")
            self.position -= 2 * length
        return value


def write(path, table):
    with open(path, "w", encoding="utf-8", newline="\n") as output:
        for key in sorted(table):
            value = table[key].replace("\\", "\\\\").replace("\r", "").replace("\n", "\n").replace("\t", "\t")
            output.write(f"{key}\t{value}\n")
    print(f"{path}: {len(table)} entries")


def main():
    system, target = sys.argv[1], sys.argv[2]

    names = {}
    records = Records(os.path.join(system, "NpcName-eu.dat"))
    for _ in range(records.count):
        npc = records.int32()
        name = records.text()
        records.text()  # nickname
        records.int32()  # colour
        if name:
            names[npc] = name
    write(os.path.join(target, "lu4_npcname.txt"), names)

    strings = {}
    records = Records(os.path.join(system, "NpcString-eu.dat"))
    for _ in range(records.count):
        key = records.int32()
        text = records.text()
        if text and text != "none":
            strings[key] = text
    write(os.path.join(target, "lu4_npcstring.txt"), strings)


if __name__ == "__main__":
    main()
