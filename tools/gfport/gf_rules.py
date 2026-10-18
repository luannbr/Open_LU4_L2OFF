"""Rules that adapt decompiled GF AI to this server's NASC.

Used by decompile_ai.py --gf <GF script folder>. The decompiler calls:
  begin_class(cls)                    before each class
  event_name(name)                    handler event -> name here, or None to leave it out
  telpos_name(text)                   teleport list entry name -> fstring ID
  rewrite_call(cls, name, args, texts) a call -> Expr, or None to keep it
  for_argument / for_pair / for_target / for_parameter   constants
and report(out_dir) at the end, which writes gf_rules.txt with every
change that loses something, so it can be reviewed.

The function signatures of this server come from NASC (nasc_metadata.py);
GF fstring.txt gives the IDs of the texts that GF passed as strings.
"""
import collections
import difflib
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import decompile_ai  # noqa: E402
import nasc_metadata  # noqa: E402

Expr = decompile_ai.Expr

# GF events with another name here, and GF events this server does not have.
EVENTS = {
    "QUESTION_MARK_CLICKED": "TUTORIAL_MARK_CLICKED",
    "CREATE_SANTA_TO_WINNER": "CREATE_NPC_TO_WINNER",
}

# Handlers of custom NPCs of the GF pack whose code was edited by hand with
# member offsets that name nothing (creature + 200 used as the name). They do
# not work in GF either and are left out.
BROKEN_HANDLERS = {
    ("e_custodian_regina", "SEE_CREATURE"), ("e_g_darkknight", "SEE_CREATURE"),
    ("e_g_deatera", "SEE_CREATURE"), ("e_g_deatera", "TALKED"), ("start_npc", "SEE_CREATURE"),
}


def read_utf16(path):
    return open(path, encoding="utf-16", errors="replace").read()


def call(name, texts):
    expr = Expr(f"{name}({', '.join(texts)})", kind="value", name=name, hint="call")
    expr.call_texts = texts
    return expr


class Rules:
    def __init__(self, gf_script):
        self.gf_script = gf_script
        self.notes = collections.defaultdict(collections.Counter)
        self.functions, self.events = nasc_metadata.load()
        self.fstrings = {}
        self.fstring_text = {}
        for line in read_utf16(os.path.join(gf_script, "fstring.txt")).splitlines():
            match = re.match(r"\s*(\d+)\s*\[(.*)\]\s*$", line)
            if match:
                self.fstrings.setdefault(match.group(2), int(match.group(1)))
                self.fstring_text[int(match.group(1))] = match.group(2)
        self.fstring_keys = list(self.fstrings)
        self.class_properties = {}
        self.properties = set()
        self.cls = None
        self.load_constants(gf_script)

    def note(self, kind, text):
        self.notes[kind][text] += 1

    # --- classes and events

    def begin_class(self, cls):
        self.cls = cls
        inherited = self.class_properties.get(cls.parent, set())
        self.properties = inherited | {name for _, name, _ in cls.properties}
        self.class_properties[cls.name] = self.properties

    def event_name(self, name):
        if (self.cls.name, name) in BROKEN_HANDLERS:
            self.note("handlers left out: broken in the GF pack", f"{self.cls.name}.{name}")
            return None
        name = EVENTS.get(name, name)
        if name not in self.events:
            self.note("handlers left out: the event does not exist here", f"{self.cls.name}.{name}")
            return None
        return name

    # --- teleport lists

    def telpos_name(self, text):
        """GF names teleport destinations by text; this server sends the
        client the fstring ID. Names that GF added itself get the closest
        fstring and are listed in the report."""
        name = text.strip('"')
        if name in self.fstrings:
            return str(self.fstrings[name])
        close = difflib.get_close_matches(name, self.fstring_keys, n=1, cutoff=0.0)
        self.note("teleport names without an fstring (closest used)", f"{name} -> {close[0]}")
        return str(self.fstrings[close[0]])

    # --- calls

    def is_global_function(self, name):
        """A GlobalObject function: called without myself. even where GF
        code edited by hand wrote myself.F()."""
        functions = self.functions.get(name)
        return bool(functions) and all(f.owner == 16 for f in functions)

    def signature(self, name, count):
        for function in self.functions.get(name, ()):
            if len(function.arguments) == count:
                return function
        return None

    @staticmethod
    def make_fstring(expr):
        """The arguments of MakeFString(id, s1..s5), or None."""
        if expr.call and expr.call[0] == "MakeFString":
            return list(expr.call[2])
        return None

    def fstring_text_expr(self, texts):
        """MakeFString as a string expression: the fstring text with its
        parameters joined in."""
        try:
            text = self.fstring_text[int(texts[0])]
        except (ValueError, KeyError):
            self.note("MakeFString with an unknown fstring", texts[0])
            return '""'
        parts = re.split(r"(\$s[1-5]|%s)", text.replace('"', "'"))
        out = []
        index = 1
        for part in parts:
            if re.fullmatch(r"\$s[1-5]|%s", part):
                number = int(part[2]) if part.startswith("$") else index
                index += 1
                out.append(texts[number])
            elif part:
                out.append(f'"{part}"')
        return " + ".join(out) or '""'

    def rewrite_call(self, cls, name, args, texts):
        texts = list(texts)
        converted = self.skill_arguments(name, args, texts)
        result = self.rewrite_call_inner(name, args, texts)
        if result is None and converted:
            result = call(name, texts)
        if result is not None and name in self.skill_returns:
            result.type = "skill"
        final = result.name if result is not None else name
        if "FStr" not in final or final == "MakeFString":
            return result
        # A text parameter of an FStr function that is an fstring without
        # parameters of its own is passed as #id (IntToFStr), as here.
        out = list(result.call_texts if result is not None and result.call_texts else texts)
        changed = False
        for position, arg in enumerate(args):
            fstring = self.make_fstring(arg)
            if fstring is None or any(x != '""' for x in fstring[1:]):
                continue
            text = self.fstring_fallback(fstring)
            if text in out:
                out[out.index(text)] = f"IntToFStr({fstring[0]})"
                changed = True
        if not changed:
            return result
        return call(final, out)

    def fstring_fallback(self, fstring):
        """The text written for a MakeFString that no rule consumed."""
        if re.fullmatch(r"-?\d+", fstring[0]):
            return self.fstring_text_expr(fstring)
        return f"IntToFStr({fstring[0]})"

    def rewrite_call_inner(self, name, args, texts):
        fixed = self.fixed_call(name, args, texts)
        if fixed is not None:
            return fixed

        # f(..., MakeFString(id, s1..s5), ...) -> fFStr(..., id, s1..s5, ...)
        for position, arg in enumerate(args):
            fstring = self.make_fstring(arg)
            if fstring is None:
                continue
            new_texts = texts[:position] + fstring + texts[position + 1:]
            for candidate in (name + "FStr", name[:-3] + "FStr" if name.endswith("Str") else None,
                              name.replace("Str", "FStr", 1) if "Str" in name else None,
                              name[:-2] + "FStrEx" if name.endswith("Ex") else None):
                if candidate and self.signature(candidate, len(new_texts)):
                    return call(candidate, new_texts)

        if name == "MakeFString":
            # Not consumed by an FStr function: the text itself (or #id when
            # the ID is computed). The call keeps its arguments for the caller.
            return Expr(self.fstring_fallback(texts), kind="value", name=name, hint="call")

        if name not in self.functions:
            # GF systems this server does not have (the fishing championship,
            # GF events): a call made for its effect is left out, a value is 0.
            self.note("calls to GF functions that do not exist here (left out, value 0)",
                      f"{name}  in {self.cls.name}")
            # Used as a value it reads 0; as a statement it becomes a comment.
            return Expr("0", kind="value", name=name, hint="removed",
                        info=f"GF {name}({', '.join(texts)}): this function does not exist here")
        function = self.signature(name, len(texts))
        if function is None:
            return None
        changed = False
        for position, (arg, kind) in enumerate(zip(args, function.arguments)):
            fstring = self.make_fstring(arg)
            if fstring is not None and kind == "string":
                self.note("MakeFString written as text", name)
            elif arg.hint == "string" and kind == "int":
                # GF passed the text where this server takes its fstring ID.
                text = arg.text[1:-1]
                if text in self.fstrings:
                    texts[position] = str(self.fstrings[text])
                else:
                    self.note("texts without an fstring (left as text)", f"{name}: {text}")
                    continue
                changed = True
        return call(name, texts) if changed else None

    def fixed_call(self, name, args, t):
        """Functions whose arguments or name changed after GF."""
        n = len(t)
        if name == "Suicide" and n == 0:
            return call(name, ["0"])
        if name == "ClassChange" and n == 2:
            return call(name, t + ["-1"])
        if name == "AddMoveSuperPointDesire" and n == 3:
            return call(name, t + ["1"])
        if name == "RenewSubJob" and n == 3:
            return call(name, t + ["0"])
        if name == "InstantZone_Leave" and n == 1:
            return call(name, t + ["0", "0", "0"])
        if name == "StartScenePlayerAround" and n == 5:
            return call(name, t[:2] + ["-1"] + t[2:])
        if name == "GiveEventItem" and n == 7:
            return call(name, t + ["@ET_DROP", "0"])
        if name == "UpdatePVPPoint" and n == 2:
            return call(name, t + ["0"])
        if name == "SetNobless" and n == 1:
            return call(name, t + ["1"])
        if name == "GetSummon" and n == 1:
            # This server's GetPet returns the pet; summons are counted apart.
            self.note("GetSummon replaced by GetPet (pets only)", self.cls.name)
            return call("GetPet", t)
        if name == "ShowQuestionMark" and n == 2:
            return call("ShowTutorialMark", [t[0], "0", t[1]])
        if name == "Talk" and n == 2:
            return call("Whisper", t)
        if name == "Teleport" and n == 8:
            footer = self.make_fstring(args[7])
            if footer is None:
                if t[7] != '""':
                    self.note("Teleport footer text dropped", t[7])
                footer = ["0", '""', '""', '""', '""', '""']
            return call("TeleportFStr", t[:7] + footer)
        if name in ("Sell", "Buy") and n == 6:
            # GF opened separate buy and sell windows; this server has one
            # window with both lists.
            lists = sorted(self.properties)
            if name == "Sell":
                buy = "BuyList0" if "BuyList0" in self.properties else \
                    next((p for p in lists if p.startswith("BuyList")), t[1])
                return call("ShowBuySell", [t[0], t[1], buy, "-50"])
            sell = "SellList0" if "SellList0" in self.properties else \
                next((p for p in lists if p.startswith("SellList")), t[1])
            return call("ShowBuySell", [t[0], sell, t[1], t[5]])
        return None

    # --- constants
    #
    # GF packed a skill as id*65536+level; this server packs
    # (id << 32) | (level << 16) | sub_level. Skill constants are written by
    # name, so NASC puts in this server's value from script_gf skill_pch.
    # A value is a skill where this server takes one: the int64 argument of
    # a skill function, skill_name_id, a class parameter holding a GF skill,
    # and variables given one of those.

    def load_constants(self, gf_script):
        root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        self.gf_skill_name = {}
        for name, key in re.findall(r"\[([^\]]+)\]\s*=\s*(\d+)", read_utf16(os.path.join(gf_script, "skill_pch.txt"))):
            self.gf_skill_name.setdefault(int(key), name)
        self.skill_names = set(re.findall(r"\[([^\]]+)\]", read_utf16(os.path.join(root, "script_gf", "skill_pch.txt"))))
        manual = read_utf16(os.path.join(gf_script, "manual_pch.txt"))
        self.gf_param_name = {int(v): n for n, v in re.findall(r"\[(PARAM_[A-Z]+)\]\s*=\s*(-?\d+)", manual)
                              if n not in ("PARAM_SKILL_MULTIPLIER",)}
        self.skill_args = {}
        for name, functions in self.functions.items():
            if not (re.search(r"Skill|^CastBuff|^GiveItemByCastleSiegeDefence$", name)):
                continue
            for function in functions:
                positions = {i for i, kind in enumerate(function.arguments) if kind == "int64"}
                if positions:
                    self.skill_args[(name, len(function.arguments))] = positions
        self.skill_returns = {"ChangeSkill_ID", "ChangeSkill_Level", "ChangeSkill_SubLevel"}
        self.skill_parameters = set()
        self.member_types = {}
        self.local_types = {}

    def is_skill(self, expr):
        return expr.type == "skill"

    def skill_constant(self, expr):
        """The skill name for a constant that is a GF skill key, or None."""
        if expr.kind != "const" or not isinstance(expr.info, int):
            return None
        return self.skill_key_name(expr.info)

    def skill_key_name(self, value):
        if value < 65536 or (value & 0xFFFF) > 1100:
            return None
        name = self.gf_skill_name.get(value)
        if name is None:
            return None
        if name not in self.skill_names:
            self.note("GF skills missing in script_gf skill_pch", name)
            return None
        return "@" + name

    def as_skill(self, expr, where):
        """A constant in a skill place, written by name."""
        name = self.skill_constant(expr)
        if name is not None:
            new = Expr(name, kind="const", info=expr.info)
            new.type = "skill"
            return new
        if expr.kind == "const" and expr.info not in (0, -1):
            self.note("numbers in skill places that are no GF skill (kept)", f"{where}: {expr.text}")
        return expr

    def begin_handler(self, handler):
        self.local_types = {}

    def type_of(self, kind, name):
        if kind == "event":
            return "skill" if name == "skill_name_id" else self.local_types.get(name)
        if kind == "member":
            return self.member_types.get((self.cls.name, name)) or self.member_types.get((None, name))
        if kind == "parameter":
            return "skill" if name in self.skill_parameters else None
        if kind == "call":
            return "skill" if name in self.skill_returns else None
        return None

    def assign(self, target, value):
        if self.is_skill(target):
            value = self.as_skill(value, f"{self.cls.name}: {target.text} =")
        if target.info is None:
            # A local variable holds what was assigned last (code order).
            if self.is_skill(value):
                self.local_types[target.text] = "skill"
            else:
                self.local_types.pop(target.text, None)
        elif self.is_skill(value):
            self.member_types[(self.cls.name, target.text)] = "skill"
        return value

    def binary(self, symbol, left, right):
        divisor = right.text in ("65536", "256 * 256") or right.text == "(256 * 256)"
        if symbol == "/" and divisor and (self.is_skill(left) or self.skill_constant(left)):
            # GF took the skill ID with key / 65536.
            left = self.as_skill(left, f"{self.cls.name}: / 65536")
            return Expr(f"GetSkill_ID({left.text})", kind="value", hint="call")
        if symbol in ("==", "!=") and (self.is_skill(left) or self.is_skill(right)):
            return self.as_skill(left, self.cls.name), self.as_skill(right, self.cls.name), None
        if symbol in ("<", "<=", ">", ">=") and right.kind == "const" and left.call \
                and left.call[0] == "GetMemoCount" and right.info == 26:
            # GF allowed 26 quests at a time; this client and server allow more.
            return left, Expr("@MAX_QUEST_NUM", kind="const", info=26), None
        if symbol in ("+", "-") and self.is_skill(left) != self.is_skill(right):
            # key + n was level + n in GF; the level is in bits 16..31 here.
            other = right if self.is_skill(left) else left
            small = other.kind != "const" or (isinstance(other.info, int) and 0 < other.info < 1000)
            if small and (symbol == "+" or other is right):
                level = Expr(f"{decompile_ai.wrap(other, 12)} * 65536", prec=12)
                if other is right:
                    return left, level, "skill"
                return level, right, "skill"
        if symbol == "&" and self.is_skill(left) and right.kind == "const":
            # key & mask read the level in GF.
            level = Expr(f"GetSkill_Level({left.text})", kind="value", hint="call")
            return level, right, None
        if self.is_skill(left) or self.is_skill(right):
            if not (symbol in ("<", "<=", ">", ">=") and "0" in (left.text, right.text)):
                self.note("arithmetic on skills (check)", f"{self.cls.name}: {left.text} {symbol} {right.text}")
        return left, right, None

    def skill_arguments(self, name, args, texts):
        positions = self.skill_args.get((name, len(args)), ())
        changed = False
        for position in positions:
            new = self.as_skill(args[position], f"{name} argument {position}")
            if new.text != texts[position]:
                texts[position] = new.text
                changed = True
        if name == "IncrementParam" and len(args) == 3 and args[1].kind == "const":
            # PARAM_LEVEL and PARAM_PKCOUNT moved when LUC and CHA were added.
            param = self.gf_param_name.get(args[1].info)
            if param:
                texts[1] = "@" + param
                changed = True
        return changed

    def for_parameter(self, cls, ptype, name, value):
        if ptype == "int" and value is not None and re.fullmatch(r"\d+", value):
            skill = self.skill_key_name(int(value))
            if skill is not None:
                self.skill_parameters.add(name)
                return skill
        return value

    def report(self, out_dir):
        with open(os.path.join(out_dir, "gf_rules.txt"), "w", encoding="utf-8") as f:
            for kind, counter in self.notes.items():
                f.write(f"{kind}: {sum(counter.values())}\n")
                for text, count in counter.most_common():
                    f.write(f"  {count}\t{text}\n")
