"""Compares two ai.obj files class by class and handler by handler.

usage: python compare_obj.py <expected ai.obj> <actual ai.obj> [report file]

Label numbers and string table numbers are renamed in order of use, so two
objects compiled from the same source compare equal. Prints the number of
classes and handlers that differ and writes the first difference of each
to the report.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import decompile_ai  # noqa: E402


def normalize(handler):
    labels = {}
    out = []
    position = {p: sorted(l for l, q in handler.labels.items() if q == p) for p in set(handler.labels.values())}
    for index, ins in enumerate(handler.code + [None]):
        for label in position.get(index, ()):
            labels.setdefault(label, f"L{len(labels)}")
            out.append(labels[label] + ":")
        if ins is None:
            break
        arg = ins.arg
        if ins.op in ("jump", "branch_false", "branch_true"):
            arg = labels.setdefault(arg, f"L{len(labels)}")
        elif ins.op == "push_string":
            arg = handler.strings.get(arg, arg)
        elif ins.op == "negate" and out and out[-1].startswith("push_const ") and "unary->" in out[-1]:
            # A negative constant from a header is one push; written as a
            # number it is a push and a negate. Both give the same value.
            _, value, comment = out[-1].split(" ", 2)
            out[-1] = f"push_const {value[1:] if value.startswith('-') else '-' + value} {comment}"
            continue
        out.append(f"{ins.op} {arg or ''} {ins.comment or ''}".rstrip())
    # Labels at the same place may be listed in any order.
    return [line for line in out if not line.endswith(":")], sorted(set(handler.variables))


def load(path):
    return {cls.name: cls for cls in decompile_ai.read_object(path)}


def main():
    expected, actual = load(sys.argv[1]), load(sys.argv[2])
    report = open(sys.argv[3], "w", encoding="utf-8") if len(sys.argv) > 3 else sys.stdout
    missing = [name for name in expected if name not in actual]
    extra = [name for name in actual if name not in expected]
    differing_classes = 0
    differing_handlers = 0
    for name, cls in expected.items():
        other = actual.get(name)
        if other is None:
            continue
        problems = []
        if cls.parent != other.parent or cls.kind != other.kind:
            problems.append(f"header {cls.kind} {cls.parent} != {other.kind} {other.parent}")
        if cls.parameters != other.parameters:
            problems.append(f"parameters differ: {set(cls.parameters) ^ set(other.parameters)}")
        if cls.properties != other.properties:
            problems.append("properties differ")
        handlers = {h.name: h for h in other.handlers}
        for handler in cls.handlers:
            mine = handlers.get(handler.name)
            if mine is None:
                problems.append(f"{handler.name}: missing")
                differing_handlers += 1
                continue
            a, va = normalize(handler)
            b, vb = normalize(mine)
            if a != b or va != vb:
                differing_handlers += 1
                if va != vb:
                    problems.append(f"{handler.name}: variables {va} != {vb}")
                for n, (x, y) in enumerate(zip(a, b)):
                    if x != y:
                        problems.append(f"{handler.name}: at {n}: expected {a[max(0, n - 3):n + 3]} got {b[max(0, n - 3):n + 3]}")
                        break
                else:
                    if len(a) != len(b):
                        problems.append(f"{handler.name}: length {len(a)} != {len(b)}")
        if problems:
            differing_classes += 1
            print(f"{name}:", file=report)
            for problem in problems:
                print(f"  {problem}", file=report)
    print(f"classes: {len(expected)} expected, {len(actual)} compiled; missing {len(missing)}, extra {len(extra)}")
    print(f"differing classes: {differing_classes}; differing handlers: {differing_handlers}")
    if missing:
        print("missing:", missing[:20])


if __name__ == "__main__":
    main()
