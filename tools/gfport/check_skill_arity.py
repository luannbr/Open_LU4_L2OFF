"""Checks every {keyword;...} item of a skilldata.txt against the argument
counts that L2Server/src/ParserSkill.y accepts for that keyword.

usage: python check_skill_arity.py <skilldata.txt> [repo root]
"""
import collections
import re
import sys

path = sys.argv[1]
root = sys.argv[2] if len(sys.argv) > 2 else "."

lexer = open(f"{root}/L2Server/src/LexerSkill.re", encoding="utf-8").read()
grammar = open(f"{root}/L2Server/src/ParserSkill.y", encoding="utf-8").read()

keywords = dict(re.findall(r'Keyword\{u"([a-z0-9_]+)", TK_([A-Z0-9_]+)', lexer))

# Alternatives of the form  TOKEN ';' a ';' b ...  -> number of arguments.
arity = collections.defaultdict(set)
for match in re.finditer(r"[|:]\s*([A-Z][A-Z0-9_]*)((?:\s+[^\n|{]*)?)\n", grammar):
    token, rest = match.group(1), match.group(2)
    rhs = rest.split("//")[0].strip()
    if not rhs:
        arity[token].add(0)
    elif rhs.startswith("';'"):
        arity[token].add(rhs.count("';'"))

text = open(path, encoding="utf-16", errors="replace").read()
ITEM = re.compile(r"\{([a-z][a-z0-9_]*)((?:;(?:\{(?:[^{}]|\{[^{}]*\})*\}|[^{};]*))*)\}")
ARG = re.compile(r";(\{(?:[^{}]|\{[^{}]*\})*\}|[^{};]*)")

bad = collections.Counter()
example = {}
for line_no, line in enumerate(text.split("\n"), 1):
    for match in ITEM.finditer(line):
        word = match.group(1)
        token = keywords.get(word)
        if token is None or token not in arity:
            continue
        count = len(ARG.findall(match.group(2)))
        if count not in arity[token]:
            key = (word, count, tuple(sorted(arity[token])))
            bad[key] += 1
            example.setdefault(key, (line_no, match.group(0)[:120]))

print(f"keywords with grammar rules: {len(arity)}")
print(f"mismatches: {sum(bad.values())} in {len(bad)} kinds")
for (word, count, allowed), n in bad.most_common():
    line_no, sample = example[(word, count, allowed)]
    print(f"  {n:6d}  {word} has {count}, grammar allows {list(allowed)}  line {line_no}: {sample}")
