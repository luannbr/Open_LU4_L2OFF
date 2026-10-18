"""Reads the function and event tables of this server's NASC from its source
(NASC/src/ScriptMetadata.cpp and ScriptTypes.cpp)."""
import os
import re

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


class Function:
    def __init__(self, owner, name, returns, arguments):
        self.owner, self.name, self.returns, self.arguments = owner, name, returns, arguments

    def __repr__(self):
        return f"{self.returns} {self.name}({', '.join(self.arguments)}) [owner {self.owner}]"


def load():
    """Returns (functions by name -> [Function], event names -> set)."""
    src = os.path.join(ROOT, "NASC", "src")
    types = dict((int(i), n) for i, n in re.findall(r'ScriptTypeDescriptor\{(\d+), u"(\w+)"',
                                                   open(os.path.join(src, "ScriptTypes.cpp"), encoding="utf-8").read()))
    text = open(os.path.join(src, "ScriptMetadata.cpp"), encoding="utf-8").read()
    arg_block = text[text.index("argumentTypeData{{"):text.index("}};", text.index("argumentTypeData{{"))]
    arg_types = [int(x) for x in re.findall(r"-?\d+", arg_block.split("{{", 1)[1])]
    functions = {}
    for owner, _, name, ret, begin, count in re.findall(
            r'ScriptFunctionDescriptor\{(\d+), (\d+), u"(\w+)", (-?\d+), (\d+), (\d+)\}', text):
        begin, count = int(begin), int(count)
        args = [types.get(t, str(t)) for t in arg_types[begin:begin + count]]
        functions.setdefault(name, []).append(Function(int(owner), name, types.get(int(ret), ret), args))
    events = set(re.findall(r'ScriptEventNameDescriptor\{\d+, -?\d+, u"(\w+)"\}', text))
    return functions, events


if __name__ == "__main__":
    import sys
    functions, events = load()
    for name in sys.argv[1:]:
        print(name, functions.get(name, "undefined"))
