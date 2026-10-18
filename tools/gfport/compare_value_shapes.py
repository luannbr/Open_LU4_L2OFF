"""Lists fields whose GF value shape never occurs in this server's file.

usage: python compare_value_shapes.py <GF file> <2018 file>

Shapes: list ({...}), name ([...]), number, word. A shape the 2018 file never
uses for a field is likely rejected by this server's parser.
"""
import collections
import re
import sys

FIELD = re.compile(r"(?:^|\t)\s*([a-z_][a-z0-9_]*)\s*=\s*(\{|\[|-?[0-9.]+(?=[\t\s]|$)|[@a-zA-Z_][^\t]*)", re.M)

def shapes(path):
    found = collections.defaultdict(collections.Counter)
    sample = {}
    text = open(path, encoding="utf-16", errors="replace").read()
    for line_no, line in enumerate(text.split("\n"), 1):
        for key, value in FIELD.findall(line):
            if value == "{":
                shape = "list"
            elif value == "[":
                shape = "name"
            elif re.fullmatch(r"-?[0-9.]+", value):
                shape = "number"
            else:
                shape = "word"
            found[key][shape] += 1
            sample.setdefault((key, shape), (line_no, re.search(re.escape(key) + r"\s*=\s*[^\t]{0,60}", line).group(0)))
    return found, sample

gf, gf_sample = shapes(sys.argv[1])
new, _ = shapes(sys.argv[2])
for key in sorted(gf):
    if key not in new:
        continue
    for shape, count in gf[key].items():
        if shape not in new[key]:
            line_no, text = gf_sample[(key, shape)]
            print(f"{key}: GF uses {shape} x{count}, this server uses {dict(new[key])}  (line {line_no}: {text})")
