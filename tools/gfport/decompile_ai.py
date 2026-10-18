"""Decompiles an ai.obj into NASC source that this server's NASC compiles.

usage: python decompile_ai.py <ai.obj> <output folder> --items <item_pch.txt>
                              [--gf <GF script folder>]

Writes npc.ai (NPC classes) and maker.ai (maker classes, compiled with the
maker event type), in the class order of the object file, and errors.txt
with every handler that could not be decompiled (those handlers are left
out of the source and listed by class).

The object file comes from the NASC compiler, so its code follows the
compiler's fixed patterns; the decompiler rebuilds statements from them:
  if:      cond; branch_false Lelse; then; [jump Lend; Lelse: else;] Lend:
  while:   Lcond: cond; branch_false Lend; body; jump Lcond; Lend:
  for:     init; Lcond: cond; branch_false Lend; jump Lbody; Linc: inc;
           jump Lcond; Lbody: body; jump Linc; Lend:
  select:  value; { dup; push_const case; equal; branch_false Lnext; body;
           jump Lnextbody; Lnext: } ... Lend: shift_sp -1
  && / ||: a; dup; branch_false/true Lx; b; and/or; Lx:
A function ID holds the argument count in bits 16..23. A call pushes its
object, then the arguments; the result replaces the object and the
following shift_sp drops the arguments.

With --gf, the rules in gf_rules.py adapt GF code to this server.
"""
import os
import re
import sys

# Handler variables that every handler has; they are not declared.
IMPLICIT_VARIABLES = {"myself", "_choiceN", "_code", "_from_choice"}
# Objects whose functions are called without a qualifier.
IMPLICIT_OBJECTS = {"myself", "gg"}

BINARY = {
    "mul": ("*", 12), "div": ("/", 12), "mod": ("%", 12),
    "add": ("+", 11), "sub": ("-", 11), "add_string": ("+", 11),
    "less": ("<", 9), "greater": (">", 9), "less_equal": ("<=", 9), "greater_equal": (">=", 9),
    "equal": ("==", 8), "not_equal": ("!=", 8),
    "bit_and": ("&", 7), "bit_or": ("|", 5),
    "and": ("&&", 4), "or": ("||", 3),
}
ATOM = 20
UNARY_PREC = 14


class DecompileError(Exception):
    pass


class Instruction:
    __slots__ = ("op", "arg", "comment")

    def __init__(self, op, arg, comment):
        self.op, self.arg, self.comment = op, arg, comment

    def __repr__(self):
        return f"{self.op} {self.arg or ''} //{self.comment or ''}"


class Expr:
    """An expression on the simulated stack."""
    __slots__ = ("text", "prec", "kind", "name", "hint", "info", "jump", "group", "call", "call_texts", "type")

    def __init__(self, text, prec=ATOM, kind="value", name=None, hint=None, info=None):
        self.text, self.prec, self.kind, self.name, self.hint, self.info = text, prec, kind, name, hint, info
        self.jump = None   # label of the && / || test made on this operand
        self.group = None  # label of the && / || chain this expression is
        self.call = None   # (function, argument expressions, argument texts) of a call
        self.call_texts = None  # argument texts of a call made by gf_rules
        self.type = None   # what the value is, when gf_rules knows (for example "skill")


def wrap(expr, prec, right=False):
    if expr.prec < prec or (right and expr.prec == prec):
        return f"({expr.text})"
    return expr.text


# ---------------------------------------------------------------- reading

LINE = re.compile(r"^\t(\w+)(?:[ \t]+([^\t/][^\t]*?))?[ \t]*(?://\s*(.*?))?\s*$")


class Handler:
    def __init__(self, name, variables, code, labels, strings):
        self.name, self.variables, self.code, self.labels, self.strings = name, variables, code, labels, strings


class AIClass:
    def __init__(self, kind, name, parent):
        self.kind, self.name, self.parent = kind, name, parent
        self.parameters = []   # (type, name, value text or None)
        self.properties = []   # ("telposlist"|"buyselllist", name, [rows])
        self.handlers = []


def read_variable(cls, line, lines):
    """Reads "type name [value]"; a string value may continue on the next lines."""
    match = re.match(r"\s+(\w+)\s+(\w+)(?:\s+(.*?))?\s*$", line)
    if not match:
        raise DecompileError(f"{cls.name}: bad variable line {line!r}")
    ptype, pname, value = match.groups()
    if value is not None and value.startswith('"'):
        while not re.search(r'"\s*$', value) or value.strip() == '"':
            value += "\n" + next(lines)
        value = value.rstrip()
    return ptype, pname, value


def read_object(path):
    """Yields the classes of an object file one at a time."""
    with open(path, encoding="utf-16", errors="strict") as f:
        lines = (line.rstrip("\r\n") for line in f)
        cls = None
        for line in lines:
            if not line.startswith("class "):
                continue
            parts = line.split()
            cls = AIClass(int(parts[1]), parts[2], None if parts[4] == "(null)" else parts[4])
            for line in lines:
                if line.startswith("class_end"):
                    break
                if line.startswith("parameter_define_begin"):
                    for line in lines:
                        if line.startswith("parameter_define_end"):
                            break
                        cls.parameters.append(read_variable(cls, line, lines))
                elif line.startswith("property_define_begin"):
                    for line in lines:
                        if line.startswith("property_define_end"):
                            break
                        match = re.match(r"\t(telposlist|buyselllist)_begin (\w+)", line)
                        if not match:
                            # A plain variable declared in the property section.
                            ptype, pname, value = read_variable(cls, line, lines)
                            cls.properties.append(("variable", pname, (ptype, value)))
                            continue
                        rows = []
                        for line in lines:
                            if line.strip().startswith(match.group(1) + "_end"):
                                break
                            rows.append(line.strip())
                        cls.properties.append((match.group(1), match.group(2), rows))
                elif line.startswith("handler "):
                    name = line.split("//")[1].strip()
                    variables, code, labels, strings = [], [], {}, {}
                    for line in lines:
                        if line.startswith("handler_end"):
                            break
                        if line.startswith("\tvariable_begin"):
                            for line in lines:
                                if line.startswith("\tvariable_end"):
                                    break
                                variables.append(line.strip().strip('"'))
                            continue
                        if not line.strip():
                            continue
                        if line[0] == "L":
                            labels[line.strip()] = len(code)
                            continue
                        if line[0] == "S":
                            key, _, text = line.partition(".\t")
                            # A string may span lines.
                            while not text.endswith('"') or len(text) < 2:
                                text += "\n" + next(lines)
                            strings[key] = text
                            continue
                        match = LINE.match(line)
                        if not match:
                            raise DecompileError(f"{cls.name}.{name}: bad line {line!r}")
                        code.append(Instruction(match.group(1), match.group(2), match.group(3)))
                    cls.handlers.append(Handler(name, variables, code, labels, strings))
            yield cls


# ---------------------------------------------------------------- decompiling

STACK_EFFECT = {
    "push_event": 1, "push_const": 1, "push_parameter": 1, "push_property": 1, "push_string": 1, "push_reg_sp": 1,
    "fetch_i": 0, "fetch_i4": 0, "fetch_d": 0, "fetch_f": 0, "negate": 0, "not": 0,
    "assign": -2, "assign4": -2, "branch_false": -1, "branch_true": -1, "jump": 0, "func_call": 0,
    "exit_handler": 0, "call_super": 0,
}
for _op in BINARY:
    STACK_EFFECT[_op] = -1


def stack_effect(ins):
    if ins.op == "shift_sp":
        return int(ins.arg)
    return STACK_EFFECT[ins.op]


class Context:
    def __init__(self, break_labels=frozenset(), continue_labels=frozenset()):
        self.break_labels, self.continue_labels = break_labels, continue_labels


class HandlerDecompiler:
    def __init__(self, handler, namer=None, cls=None):
        self.h = handler
        self.cls = cls
        self.used = set()  # event variables the code uses
        self.code = handler.code
        self.namer = namer
        # labels at each position
        self.at = {}
        for label, pos in handler.labels.items():
            self.at.setdefault(pos, set()).add(label)
        # instructions that branch or jump to each position
        self.sources = {}
        for index, ins in enumerate(self.code):
            if ins.op in ("jump", "branch_false", "branch_true"):
                self.sources.setdefault(self.pos(ins.arg), []).append(index)

    def ends_inside(self, i, t):
        """True when a structure inside (i, t-1) also ends at t; the jump
        before t is then that structure's, not an else of the branch at i."""
        return any(i < j < t - 1 for j in self.sources.get(t, ()))

    def pos(self, label):
        try:
            return self.h.labels[label]
        except KeyError:
            raise DecompileError(f"unknown label {label}")

    def fail(self, i, message):
        context = "; ".join(repr(x) for x in self.code[max(0, i - 3):i + 3])
        raise DecompileError(f"{message} at {i}: {context}")

    def run(self):
        if self.namer:
            self.namer.begin_handler(self.h)
        lines = []
        self.block(0, len(self.code), Context(), lines, 2)
        return lines

    # --- statements

    def block(self, i, end, ctx, out, depth):
        stack = []
        statement_start = i
        ind = "\t" * depth
        while i < end:
            ins = self.code[i]
            op = ins.op
            if not stack:
                statement_start = i
            if op in ("branch_false", "branch_true") and len(stack) == 1:
                cond = stack.pop()
                self.check_value(i, cond)
                if op == "branch_true":
                    # Only code edited by hand branches on true here.
                    cond = Expr(f"{wrap(cond, BINARY['equal'][1])} == 0", prec=BINARY["equal"][1])
                i = self.branch(i, statement_start, cond, end, ctx, out, depth)
                continue
            if op == "push_reg_sp" and self.code[i + 1].comment == "select_case compare":
                if len(stack) != 1:
                    self.fail(i, "select with a deep stack")
                i = self.select(i, stack.pop(), end, ctx, out, depth)
                continue
            if op == "shift_sp" and not stack:
                self.fail(i, "stack underflow")
            if op == "shift_sp" and ins.arg == "-1" and len(stack) == 1:
                expr = stack.pop()
                if expr.hint == "removed":
                    out.append(f"{ind}// {expr.info}")
                elif expr.kind == "value" and expr.hint != "call" and re.fullmatch(r"[\w.]+", expr.text):
                    # A value read and dropped is a select without cases.
                    out.append(f"{ind}select ({expr.text})")
                    out.append(f"{ind}{{")
                    out.append(f"{ind}}}")
                else:
                    out.append(f"{ind}{self.statement_text(expr)};")
                i += 1
                continue
            if op == "jump" and not stack:
                target = ins.arg
                if target in ctx.break_labels:
                    out.append(f"{ind}break;")
                elif target in ctx.continue_labels:
                    out.append(f"{ind}continue;")
                else:
                    self.fail(i, "jump outside a known structure")
                i += 1
                continue
            if op == "exit_handler" and not stack:
                out.append(f"{ind}return;")
                i += 1
                continue
            if op == "call_super" and not stack:
                out.append(f"{ind}super;")
                i += 1
                continue
            i = self.expression_step(i, stack)
        if stack:
            self.fail(end, f"{len(stack)} values left at the end of a block")

    def statement_text(self, expr):
        return expr.text

    def branch(self, i, start, cond, end, ctx, out, depth):
        ind = "\t" * depth
        target = self.code[i].arg
        t = self.pos(target)
        if t <= i:
            self.fail(i, "branch backwards")
        if t > end:
            self.fail(i, "branch leaves the block")
        start_labels = self.at.get(start, set())
        nxt = self.code[i + 1] if i + 1 < len(self.code) else None
        before = self.code[t - 1] if t - 1 > i else None

        # for (init; cond; inc)
        if nxt is not None and nxt.op == "jump" and self.pos(nxt.arg) > i + 1 and before is not None \
                and before.op == "jump" and start_labels:
            body_start = self.pos(nxt.arg)
            inc_label = before.arg
            inc_start = self.pos(inc_label)
            jump_back = body_start - 1
            if inc_start == i + 2 and self.code[jump_back].op == "jump" and self.code[jump_back].arg in start_labels:
                inc_lines = []
                self.block(inc_start, jump_back, Context(), inc_lines, 0)
                increment = self._as_expression(inc_lines, "")
                if increment is None:
                    self.fail(i, "for increment is not one expression")
                # The statement before the loop is its initialisation.
                init = self._as_expression(out, ind)
                if init is None:
                    self.fail(i, "for without an initialisation")
                out.append(f"{ind}for ({init}; {cond.text}; {increment})")
                out.append(f"{ind}{{")
                self.block(body_start, t - 1, Context(frozenset([target]), frozenset([inc_label])), out, depth + 1)
                out.append(f"{ind}}}")
                return t

        # while (cond)
        if before is not None and before.op == "jump" and before.arg in start_labels:
            out.append(f"{ind}while ({cond.text})")
            out.append(f"{ind}{{")
            self.block(i + 1, t - 1, Context(frozenset([target]), frozenset([before.arg])), out, depth + 1)
            out.append(f"{ind}}}")
            return t

        # if (cond) ... else ...
        if before is not None and before.op == "jump" and before.arg not in ctx.break_labels \
                and before.arg not in ctx.continue_labels and self.pos(before.arg) >= t \
                and not self.ends_inside(i, t):
            else_end = self.pos(before.arg)
            if else_end > end:
                self.fail(i, "else leaves the block")
            out.append(f"{ind}if ({cond.text})")
            out.append(f"{ind}{{")
            self.block(i + 1, t - 1, ctx, out, depth + 1)
            out.append(f"{ind}}}")
            else_lines = []
            self.block(t, else_end, ctx, else_lines, depth + 1)
            # else { if ... } is written as else if
            if else_lines and else_lines[0].strip().startswith("if (") and self._single_statement(else_lines, depth + 1):
                out.append(f"{ind}else {else_lines[0].strip()}")
                out.extend(line[1:] for line in else_lines[1:])
            else:
                out.append(f"{ind}else")
                out.append(f"{ind}{{")
                out.extend(else_lines)
                out.append(f"{ind}}}")
            return else_end

        out.append(f"{ind}if ({cond.text})")
        out.append(f"{ind}{{")
        self.block(i + 1, t, ctx, out, depth + 1)
        out.append(f"{ind}}}")
        return t

    def _as_expression(self, lines, ind):
        """Takes the last statement of lines when it is one expression.
        A value read alone was written as an empty select; inside a for it
        is a plain expression."""
        if len(lines) >= 3 and lines[-3].startswith(ind + "select (") and lines[-2:] == [ind + "{", ind + "}"] \
                and not lines[-3].startswith(ind + "\t"):
            text = lines[-3].strip()[len("select ("):-1]
            del lines[-3:]
            return text
        if lines and lines[-1].startswith(ind) and not lines[-1].startswith(ind + "\t") \
                and lines[-1].endswith(";") and self._is_expression(lines[-1]):
            return lines.pop().strip()[:-1]
        return None

    @staticmethod
    def _is_expression(line):
        text = line.strip()
        return not re.match(r"(if|else|while|for|select|case|break|continue|return|super)\b|[{}]", text)

    @staticmethod
    def _single_statement(lines, depth):
        """True when lines hold one if/else chain at the given depth."""
        ind = "\t" * depth
        first = True
        for line in lines:
            if line.startswith(ind) and not line.startswith(ind + "\t"):
                text = line.strip()
                if first:
                    first = False
                    continue
                if text in ("{", "}") or text.startswith("else"):
                    continue
                return False
        return True

    def select(self, i, selector, end, ctx, out, depth):
        ind = "\t" * depth
        self.check_value(i, selector)
        # The select ends where its value is dropped.
        level = 1
        j = i
        while True:
            if j >= end:
                self.fail(i, "select without an end")
            level += stack_effect(self.code[j])
            if level == 0:
                break
            j += 1
        if self.code[j].op != "shift_sp":
            self.fail(j, "select does not end with shift_sp")
        select_end = j
        break_labels = frozenset(self.at.get(select_end, set()))
        out.append(f"{ind}select ({selector.text})")
        out.append(f"{ind}{{")
        p = i
        while True:
            code = self.code
            if not (code[p].op == "push_reg_sp" and code[p + 1].comment == "select_case compare"):
                self.fail(p, "select case expected")
            q = p + 2
            value_stack = []
            while code[q].op != "equal":
                q = self.expression_step(q, value_stack)
            if len(value_stack) != 1:
                self.fail(q, "select case value")
            value = value_stack[0].text
            if code[q + 1].op != "branch_false":
                self.fail(q, "select case without branch")
            next_case = self.pos(code[q + 1].arg)
            body_start = q + 2
            if code[next_case - 1].op != "jump":
                self.fail(next_case, "case without fall-through jump")
            out.append(f"{ind}\tcase {value}:")
            self.block(body_start, next_case - 1, Context(break_labels, ctx.continue_labels), out, depth + 2)
            fall = self.pos(code[next_case - 1].arg)
            if next_case >= select_end or not (code[next_case].op == "push_reg_sp" and
                                                code[next_case + 1].comment == "select_case compare"):
                # last case: only labels may follow until the end
                if next_case != select_end or fall != select_end:
                    self.fail(next_case, "code after the last case")
                break
            p = next_case
        out.append(f"{ind}}}")
        return select_end + 1

    # --- expressions

    def expression_step(self, i, stack):
        ins = self.code[i]
        op = ins.op
        code = self.code
        if op == "push_event":
            self.used.add(ins.comment)
            stack.append(Expr(ins.comment, kind="event", name=ins.comment))
            return i + 1
        if op == "push_const":
            comment = ins.comment or ""
            nxt = code[i + 1].op if i + 1 < len(code) else None
            if stack and stack[-1].kind == "event" and nxt == "add":
                base = stack.pop()
                addr = Expr(base.name, kind="addr", name=base.name, hint=comment)
                addr.type = self.type_of("event", base.name)
                stack.append(addr)
                return i + 2
            if comment.startswith("unary->") or comment in ("select_case", "++", "--"):
                stack.append(self.constant(ins, comment))
                return i + 1
            if not comment:
                # Code edited by hand has no comments. An offset added to an
                # object and then read or assigned is a member; the read of the
                # object before it names the member.
                after = code[i + 2].op if i + 2 < len(code) else None
                member = nxt == "add" and stack and stack[-1].kind == "value" and stack[-1].hint != "call" \
                    and after in ("fetch_i", "fetch_i4", "fetch_d", "fetch_f", "push_reg_sp") \
                    and code[i - 1].op.startswith("fetch") and code[i - 1].comment
                if not member:
                    kind = "unary->FLOAT_CONST" if "." in ins.arg else "unary->INTEGER_CONST"
                    stack.append(self.constant(ins, kind))
                    return i + 1
                comment = code[i - 1].comment
            if nxt == "add" and stack and stack[-1].kind == "value":
                base = stack.pop()
                addr = Expr(f"{wrap(base, ATOM)}.{comment}", kind="addr", name=comment, info=base)
                addr.type = self.type_of("member", addr.text)
                stack.append(addr)
                return i + 2
            self.fail(i, "unknown constant use")
        if op in ("fetch_i", "fetch_i4", "fetch_d", "fetch_f"):
            if stack and stack[-1].kind == "assigned":
                # An 8-byte assignment leaves the address; its value is fetched.
                stack[-1].kind = "value"
                return i + 1
            if stack and stack[-1].hint == "call" and code[i + 2].op == "add":
                # An object returned by a call is fetched before a member access.
                return i + 1
            if not stack or stack[-1].kind != "addr":
                self.fail(i, "fetch without an address")
            addr = stack.pop()
            value = Expr(addr.text, kind="value", name=addr.name, hint=addr.hint, info=addr.info)
            value.type = addr.type
            stack.append(value)
            return i + 1
        if op == "push_reg_sp":
            comment = code[i + 1].comment
            if code[i + 1].op != "fetch_i":
                self.fail(i, "push_reg_sp without fetch")
            if not comment:
                # Code edited by hand: tell the uses apart by what follows.
                ops = [x.op for x in code[i + 2:i + 9]]
                if ops[:1] in (["branch_false"], ["branch_true"]):
                    comment = "and list" if ops[0] == "branch_false" else "or exit label"
                elif ops[:3] == ["push_reg_sp", "fetch_i", "fetch_i"] and ops[4:5] in (["add"], ["sub"]):
                    comment = "++" if ops[4] == "add" else "--"
                elif stack and stack[-1].kind == "addr":
                    comment = "expr ="
            if code[i + 2].op in ("branch_false", "branch_true") and comment in ("and list", "or exit label"):
                stack[-1].jump = code[i + 2].arg
                return i + 3
            if comment == "expr =":
                target = stack.pop()
                if target.kind != "addr":
                    self.fail(i, "assignment to a value")
                assign_target = Expr(target.text, kind="target", name=target.name, info=target.info)
                assign_target.type = target.type
                stack.append(assign_target)
                return i + 2
            if comment in ("++", "--"):
                # addr; dup; dup; fetch; push_const 1; add|sub; assign; fetch (after)
                target = stack.pop()
                sign = "++" if comment == "++" else "--"
                k = i + 2
                expected = ["push_reg_sp", "fetch_i", "fetch_i", "push_const", "add" if sign == "++" else "sub",
                            "assign" if True else None]
                ops = [code[k + n].op for n in range(6)]
                if ops[:5] != expected[:5] or ops[5] not in ("assign", "assign4"):
                    self.fail(i, f"unexpected {sign} pattern {ops}")
                after = code[k + 6]
                if after.op not in ("fetch_i", "fetch_i4", "fetch_d", "fetch_f"):
                    self.fail(i, f"{sign} without fetch after assign")
                stack.append(Expr(f"{sign}{target.text}", prec=UNARY_PREC))
                return k + 7
            self.fail(i, f"unknown push_reg_sp use ({comment})")
        if op == "push_parameter":
            value = Expr(ins.arg, kind="value", name=ins.arg, hint="parameter")
            value.type = self.type_of("parameter", ins.arg)
            stack.append(value)
            return i + 1
        if op == "push_property":
            stack.append(Expr(ins.arg, kind="value", name=ins.arg, hint="property"))
            return i + 1
        if op == "push_string":
            text = self.h.strings.get(ins.arg)
            if text is None:
                self.fail(i, "unknown string")
            stack.append(Expr(text, kind="value", hint="string"))
            return i + 1
        if op in ("assign", "assign4"):
            value = stack.pop()
            self.check_value(i, value)
            target = stack.pop()
            if target.kind != "target":
                self.fail(i, "assign without target")
            if self.namer:
                value = self.namer.assign(target, value)
            stack.append(Expr(f"{target.text} = {value.text}", prec=1, kind="assigned"))
            return i + 1
        if op in BINARY:
            right = stack.pop()
            left = stack.pop()
            self.check_value(i, left)
            self.check_value(i, right)
            symbol, prec = BINARY[op]
            # A string member is used by its address (no fetch).
            if right.kind not in ("value", "const", "addr") or left.kind not in ("value", "const", "addr"):
                self.fail(i, f"{op} with an address")
            result_type = None
            if self.namer:
                rewritten = self.namer.binary(symbol, left, right)
                if not isinstance(rewritten, tuple):
                    stack.append(rewritten)
                    return i + 1
                left, right, result_type = rewritten
            # One && / || chain shares one label; a chain with its own label
            # was written in parentheses.
            group = left.jump if op in ("and", "or") else None
            if group is not None and left.group is not None and left.group != group and left.prec == prec:
                left_text = f"({left.text})"
            else:
                left_text = wrap(left, prec)
            right_text = wrap(right, prec, right=True)
            result = Expr(f"{left_text} {symbol} {right_text}", prec=prec)
            result.group = group
            result.type = result_type
            stack.append(result)
            return i + 1
        if op == "negate":
            value = stack.pop()
            if value.kind == "const":
                # A negative constant negated (code edited by hand) is positive.
                text = value.text[1:] if value.text.startswith("-") else f"-{value.text}"
                stack.append(Expr(text, kind="const", info=-value.info))
            else:
                stack.append(Expr(f"-{wrap(value, UNARY_PREC)}", prec=UNARY_PREC))
            return i + 1
        if op == "not":
            value = stack.pop()
            stack.append(Expr(f"~{wrap(value, UNARY_PREC)}", prec=UNARY_PREC))
            return i + 1
        if op == "func_call":
            name = re.search(r"func\[(\w+)\]", ins.comment).group(1)
            function_id = int(ins.arg)
            argc = (function_id >> 16) & 0xFF
            if len(stack) < argc + 1:
                self.fail(i, "call with too few values")
            args = stack[len(stack) - argc:] if argc else []
            for arg in args:
                self.check_value(i, arg)
            del stack[len(stack) - argc:]
            receiver = stack.pop()
            arg_texts = [a.text for a in args]
            if receiver.kind != "value":
                self.fail(i, "call on an address")
            # Code edited by hand has no comment naming the function.
            # myself.F() is written as such; gg.F() is not valid source.
            plain = receiver.name in IMPLICIT_OBJECTS and receiver.info is None
            implicit = plain and (receiver.hint in (name, "", None) or receiver.hint == receiver.name == "gg"
                                  or (self.namer is not None and self.namer.is_global_function(name)))
            expr = None
            if self.namer and plain:
                expr = self.namer.rewrite_call(self.cls, name, args, arg_texts)
                if expr is not None and not implicit:
                    expr.text = f"{receiver.text}.{expr.text}"
            if expr is None:
                if implicit:
                    text = f"{name}({', '.join(arg_texts)})"
                else:
                    text = f"{wrap(receiver, ATOM)}.{name}({', '.join(arg_texts)})"
                expr = Expr(text, kind="value", name=name, hint="call")
                expr.type = self.type_of("call", name)
            expr.call = (name, args, arg_texts)
            stack.append(expr)
            i += 1
            if argc:
                if code[i].op != "shift_sp" or int(code[i].arg) != -argc:
                    self.fail(i, f"call to {name} not followed by shift_sp -{argc}")
                i += 1
            return i
        self.fail(i, f"unexpected {op}")

    def check_value(self, i, expr):
        if expr.hint == "removed":
            self.fail(i, f"uses a value of {expr.info}")

    def constant(self, ins, comment):
        text = ins.arg
        if comment == "unary->FLOAT_CONST":
            return Expr(text, kind="const", info=float(text), hint="float")
        return Expr(text, kind="const", info=int(text))

    def type_of(self, kind, name):
        return self.namer.type_of(kind, name) if self.namer else None


# ---------------------------------------------------------------- writing

def parameter_source(ptype, name, value):
    if ptype == "waypointstype":
        return f"\tWayPointsType {name};"
    if ptype == "waypointdelaystype":
        return f"\tWayPointDelaysType {name};"
    if value is None:
        return f"\t{ptype} {name};"
    return f"\t{ptype} {name} = {value};"


ITEM_NAMES = {}


def load_item_names(path):
    text = open(path, encoding="utf-16", errors="replace").read()
    for name, item_id in re.findall(r"\[([^\]]+)\]\s*=\s*(\d+)", text):
        ITEM_NAMES.setdefault(int(item_id), name)


def property_source(kind, name, rows, namer):
    if kind == "variable":
        return parameter_source(rows[0], name, rows[1])
    if kind == "telposlist":
        items = []
        for row in rows:
            fields = [f.strip() for f in row.strip("{} ").split(";")]
            if "{" in row.strip()[1:]:
                # GF lists several castles for the tax ({4; 5}); this server takes one.
                inner = row.strip()[1:-1]
                inner = re.sub(r"\{\s*(-?\d+)[^{}]*\}", r"\1", inner)
                fields = [f.strip() for f in inner.split(";")]
            if namer and fields[0].startswith('"'):
                fields[0] = namer.telpos_name(fields[0])
            # The sixth field is optional in the source and 0 when left out.
            items.append("{" + ";".join(fields if fields[5:] != ["0"] else fields[:5]) + "}")
        return f"\tTelPosList {name} = {{{';'.join(items)}}};"
    items = []
    for row in rows:
        fields = [f.strip() for f in row.strip("{} ").split(";")]
        # The source names items; the object keeps their IDs.
        item = ITEM_NAMES.get(int(fields[0]))
        if item is None:
            raise DecompileError(f"{name}: item {fields[0]} has no name in item_pch")
        # A bare name lists the item; {name;count} or {name;count;rate;n} add more.
        if len(fields) == 2:
            items.append(f'"{item}"')
        elif fields[2:] == ["0.000000", "0"]:
            items.append(f'{{"{item}";{fields[1]}}}')
        else:
            items.append("{" + f'"{item}"' + ";" + ";".join(fields[1:]) + "}")
    return f"\tBuySellList {name} = {{{';'.join(items)}}};"


def class_source(cls, namer, errors):
    if namer:
        namer.begin_class(cls)
    out = [f"class {cls.name}" + (f" : {cls.parent}" if cls.parent else ""), "{"]
    if cls.parameters:
        out.append("parameter:")
        for ptype, name, value in cls.parameters:
            if namer:
                value = namer.for_parameter(cls, ptype, name, value)
            out.append(parameter_source(ptype, name, value))
    if cls.properties:
        out.append("property:")
        for kind, name, rows in cls.properties:
            out.append(property_source(kind, name, rows, namer))
    if cls.handlers:
        out.append("handler:")
        for handler in cls.handlers:
            decompiler = HandlerDecompiler(handler, namer, cls)
            try:
                body = decompiler.run()
            except (DecompileError, IndexError, KeyError) as error:
                errors.append(f"{cls.name}.{handler.name}: {error}")
                continue
            # Code edited by hand may use variables it did not declare.
            params = [v for v in handler.variables if v not in IMPLICIT_VARIABLES]
            params += sorted(v for v in decompiler.used
                             if v not in params and v not in IMPLICIT_VARIABLES and v not in IMPLICIT_OBJECTS)
            name = namer.event_name(handler.name) if namer else handler.name
            if name is None:
                errors.append(f"{cls.name}.{handler.name}: the event does not exist in this server")
                continue
            out.append(f"\tEventHandler {name}({', '.join(params)})")
            out.append("\t{")
            out.extend(body)
            out.append("\t}")
            out.append("")
    out.append("}")
    out.append("")
    return out


def main():
    args = sys.argv[1:]
    namer = None
    if "--gf" in args:
        index = args.index("--gf")
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import gf_rules
        namer = gf_rules.Rules(args[index + 1])
        del args[index:index + 2]
    if "--items" in args:
        index = args.index("--items")
        load_item_names(args[index + 1])
        del args[index:index + 2]
    source, out_dir = args
    os.makedirs(out_dir, exist_ok=True)
    errors = []
    counts = {"classes": 0, "maker": 0, "handlers": 0}
    with open(os.path.join(out_dir, "npc.ai"), "w", encoding="utf-16", newline="\r\n") as npc, \
            open(os.path.join(out_dir, "maker.ai"), "w", encoding="utf-16", newline="\r\n") as maker:
        maker.write("set_compiler_opt base_event_type(@NTYPE_MAKER_EVENT)\n\n")
        seen = set()
        for cls in read_object(source):
            if cls.name in seen:
                errors.append(f"{cls.name}: defined again; the first definition is kept")
                continue
            seen.add(cls.name)
            counts["classes"] += 1
            counts["maker"] += cls.kind == 1
            counts["handlers"] += len(cls.handlers)
            (maker if cls.kind == 1 else npc).write("\n".join(class_source(cls, namer, errors)) + "\n")
    with open(os.path.join(out_dir, "errors.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(errors) + ("\n" if errors else ""))
    print(f"classes: {counts['classes']} ({counts['maker']} maker); handlers: {counts['handlers']}; "
          f"not decompiled: {len(errors)}")
    if namer:
        namer.report(out_dir)


if __name__ == "__main__":
    main()
