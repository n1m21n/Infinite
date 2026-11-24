#!/usr/bin/env python3
"""Move node-body controls onto params::Row tables drawn by Ui::DrawParams / Ui::DrawKnobs (ui-formalisation
Block C; src/app/graph/ParamDesc.h, src/app/ui/design/DrawParam.h).

A run of two or more consecutive one-line control calls on the body's node becomes one table, placed in an
anonymous namespace just above the function, and the run becomes one draw call:

  ModSlider("size x", &n->sizeX, params::Shape::size_x_lo, params::Shape::size_x_hi);    stacked rows
  ModSliderInt / ModCheckbox / ColorSwatch("fill", n->fill, n) / DropdownButton(.., n->m, [n](int i) { n->m = i; })
  row.Knob("delay", n->ParamPtr("delay"), lo, hi, "%.1f ms", kKnobLarge);                 knob rows (AudioKnobRow)
  row.KnobInt(...)

Pixel / ordinal rule: a row makes exactly the call the line made (DrawParam.h), and rows draw in table order, so a
run converts only when every line in it converts. A line converts only when every argument is a compile-time
constant the table can hold - a literal, a string literal, params:: / tok:: names, a namespace-scope k-constant,
a captureless lambda, nullptr / true / false - and its value is `&n->member`, `n->ParamPtr("..")` or (colour)
`n->member`. kParamWidth is live (StageNodeBodies stretches it), so it maps to "the widget's default", never into a
table. Anything else stays hand-written and is listed with --report.

    python3 tools/design/param_desc_migrate.py [--dry] [--report] [--file src/app/bodies/X.cpp]
"""
import argparse, os, re, sys, collections

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCOPE = "src/app/bodies/"
INCLUDE = '#include "app/ui/design/DrawParam.h"'

FUNC = re.compile(r'^   (?:static\s+)?(?:void|bool|float|int)\s+(Draw\w+)\(([^)]*)\)\s*$', re.M)
NODEARG = re.compile(r'(?:^|,)\s*(?:const\s+)?([A-Z]\w*)\s*\*\s*(\w+)\s*(?:,|$)')
KINDS = r'(ModSlider|ModSliderInt|ModCheckbox|ColorSwatch|DropdownButton|Knob|KnobInt|Dropdown|Checkbox)'
CALL = re.compile(r'^(\s*)(?:(\w+)\.)?' + KINDS + r'\((.*)\);\s*(//.*)?$', re.S)
CALL_START = re.compile(r'^\s*(?:\w+\.)?' + KINDS + r'\(')
CELL = re.compile(r'^(\s*)(\w+)\.index\s*=\s*([^;]+);\s*$')
CONST_ATOM = re.compile(r'''^(?:
      -?[0-9]+(?:\.[0-9]*)?(?:e-?[0-9]+)?f?      # number
    | params::[\w:]+ | tok::\w+                   # tables / tokens
    | k[A-Z]\w*                                    # k-constants (checked: not local, not kParamWidth)
    | nullptr | true | false
    | AudioWidgetStyle::Knob
    | [A-Z]\w*(?:::\w+)+                           # Namespace::Fn (taper functions)
    )$''', re.X)


def split_args(s):
    out, depth, cur, q = [], 0, "", None
    i = 0
    while i < len(s):
        ch = s[i]
        if q:
            cur += ch
            if ch == "\\":
                cur += s[i + 1]; i += 2; continue
            if ch == q:
                q = None
        elif ch in "\"'":
            q = ch; cur += ch
        elif ch in "([{":
            depth += 1; cur += ch
        elif ch in ")]}":
            depth -= 1; cur += ch
        elif ch == "," and depth == 0:
            out.append(cur.strip()); cur = ""
        else:
            cur += ch
        i += 1
    if cur.strip():
        out.append(cur.strip())
    return out


def is_string(a):
    return re.fullmatch(r'"(?:[^"\\]|\\.)*"(?:\s*"(?:[^"\\]|\\.)*")*', a) is not None


class Reject(Exception):
    pass


class Ctx:
    def __init__(self, typ, var, body, rowvars, filesrc):
        self.typ, self.var, self.body, self.rowvars, self.filesrc = typ, var, body, rowvars, filesrc
        # locals that are the live param width under another name (`const float colW = kParamWidth;`)
        self.width_alias = set(re.findall(r'\bconst float (\w+) = kParamWidth;', body))
        # file-scope functions (taper maps) a table may point at
        self.file_fns = set(re.findall(r'^   (?:static\s+)?(?:inline\s+)?float (\w+)\(float \w+, float \w+, float \w+\)', filesrc, re.M))

    def width(self, a):
        return "kParamWidth" if a.strip() in self.width_alias else a.strip()

    def const(self, a):
        a = a.strip()
        if is_string(a):
            return a
        if re.fullmatch(r'\[\]\s*\([^)]*\)\s*(?:->\s*[\w:*& ]+)?\s*\{.*\}', a) and self.var + "->" not in a:
            return a  # captureless lambda
        # simple arithmetic of constant atoms, parenthesised or not
        if a in self.file_fns:
            return a
        toks = [t for t in re.split(r'\s*[-+*/()]\s*', a) if t]
        if not toks:
            raise Reject("empty arg")
        for t in toks:
            if not CONST_ATOM.match(t) and not re.fullmatch(r'[0-9]+(?:\.[0-9]*)?f?', t):
                raise Reject(f"not constant: {a}")
            if t == "kParamWidth":
                raise Reject("kParamWidth inside an expression")
            if re.fullmatch(r'k[A-Z]\w*', t) and re.search(r'\b(?:const|constexpr|float|int|auto)\s+(?:float\s+|int\s+)?' + t + r'\b\s*=', self.body):
                raise Reject(f"local constant {t}")
        if re.search(r'\b(?:std::|ImGui::)', a):
            raise Reject(f"runtime call: {a}")
        return a

    def getter(self, a, want):
        """want: 'f' float*, 'i' int*, 'b' bool*, 'arr' colour array. Returns (template_member or None, lambda or None)."""
        v = re.escape(self.var)
        m = re.fullmatch(r'&\s*' + v + r'->(\w+)', a)
        if m and want != "arr":
            return f"&{self.typ}::{m.group(1)}", None
        m = re.fullmatch(v + r'->(\w+)', a)
        if m and want == "arr":
            return f"&{self.typ}::{m.group(1)}", None
        m = re.fullmatch(r'&\s*' + v + r'->([\w.]+(?:\[[0-9]+\])?(?:\.\w+)*)', a)
        if m and want != "arr":
            return None, f"[]({self.typ}* {self.var}) {{ return &{self.var}->{m.group(1)}; }}"
        m = re.fullmatch(v + r'->(ParamPtr\("[^"]*"\))', a)
        if m and want == "f":
            return None, f"[]({self.typ}* {self.var}) {{ return {self.var}->{m.group(1)}; }}"
        m = re.fullmatch(v + r'->([\w.]+(?:\[[0-9]+\])?(?:\.\w+)*)', a)
        if m and want == "arr":
            return None, f"[]({self.typ}* {self.var}) {{ return {self.var}->{m.group(1)}; }}"
        raise Reject(f"value: {a}")


def row_text(ctx, alias, kind, args):
    """Builds `Alias::Kind<...>(...)...` for one call, or raises Reject."""
    mods = []

    def head(k, label, val, want, rest):
        mem, lam = ctx.getter(val, want)
        if mem:
            return f"{alias}::{k}<{mem}>({label}{rest})"
        return f"{alias}::{k}({label}, {lam}{rest})"

    if kind in ("ModSlider", "Knob"):
        if len(args) < 4:
            raise Reject("short")
        label = args[0]
        if not is_string(label):
            raise Reject("label not literal")
        lo, hi = ctx.const(args[2]), ctx.const(args[3])
        rest = args[4:]
        names = (["fmt", "width", "audio", "step", "p2v", "v2p", "idx", "name"] if kind == "ModSlider" else
                 ["fmt", "dia", "db", "freq", "style", "p2v", "v2p", "idx", "name"])
        if len(rest) > len(names):
            raise Reject("too many args")
        vals = dict(zip(names, rest))
        if kind == "Knob" and "fmt" not in vals:
            raise Reject("knob without fmt")
        if "fmt" in vals:
            f = ctx.const(vals["fmt"])
            if not (kind == "ModSlider" and f == '"%.3f"'):
                if kind == "Knob" and not is_string(f):
                    raise Reject("knob fmt not literal")
                mods.append(f".Fmt({f})")
        size = vals.get("width") or vals.get("dia")
        if size is not None:
            size = ctx.width(size)
            if size == "kParamWidth" and kind == "ModSlider":
                pass
            elif size == "kKnobSmall" and kind == "Knob":
                pass
            else:
                mods.append(f".Size({ctx.const(size)})")
        if "audio" in vals:
            ctx.const(vals["audio"])  # forced true inside ModSlider; any constant is the same call
        if vals.get("step") not in (None, "0", "0.0f", "0.0"):
            mods.append(f".Step({ctx.const(vals['step'])})")
        if vals.get("db") not in (None, "false"):
            if vals["db"] != "true":
                raise Reject("db not literal")
            mods.append(".Db()")
        if vals.get("freq") not in (None, "false"):
            if vals["freq"] != "true":
                raise Reject("freq not literal")
            if ".Db()" in mods:
                raise Reject("db and freq")
            mods.append(".Freq()")
        if vals.get("style") not in (None, "AudioWidgetStyle::Knob"):
            raise Reject("knob style")
        p2v, v2p = vals.get("p2v"), vals.get("v2p")
        if (p2v or v2p) and not (p2v in (None, "nullptr") and v2p in (None, "nullptr")):
            mods.append(f".Curve({ctx.const(p2v or 'nullptr')}, {ctx.const(v2p or 'nullptr')})")
        if vals.get("idx") not in (None, "-1"):
            mods.append(f".At({ctx.const(vals['idx'])})")
        if vals.get("name") not in (None, "nullptr"):
            mods.append(f".Named({ctx.const(vals['name'])})")
        return head("Slider" if kind == "ModSlider" else "Knob", label, args[1], "f", f", {lo}, {hi}") + "".join(mods)

    if kind in ("ModSliderInt", "KnobInt"):
        if len(args) < 4 or not is_string(args[0]):
            raise Reject("short / label")
        lo, hi = ctx.const(args[2]), ctx.const(args[3])
        rest = args[4:]
        names = ["width", "audio", "fmt"] if kind == "ModSliderInt" else ["dia"]
        if len(rest) > len(names):
            raise Reject("too many args")
        vals = dict(zip(names, rest))
        size = vals.get("width") or vals.get("dia")
        if size is not None:
            size = ctx.width(size)
        if size is not None and not ((size == "kParamWidth" and kind == "ModSliderInt") or (size == "kKnobSmall" and kind == "KnobInt")):
            mods.append(f".Size({ctx.const(size)})")
        if "audio" in vals:
            ctx.const(vals["audio"])
        if vals.get("fmt") not in (None, '"%.0f"'):
            mods.append(f".Fmt({ctx.const(vals['fmt'])})")
        return head("SliderInt" if kind == "ModSliderInt" else "KnobInt", args[0], args[1], "i", f", {lo}, {hi}") + "".join(mods)

    if kind == "ModCheckbox":
        if len(args) != 2 or not is_string(args[0]):
            raise Reject("checkbox args")
        return head("Toggle", args[0], args[1], "b", "")

    if kind == "ColorSwatch":
        if len(args) != 3 or not is_string(args[0]) or args[2] != ctx.var:
            raise Reject("swatch args")
        return head("Colour", args[0], args[1], "arr", "")

    if kind in ("DropdownButton", "Dropdown"):
        if len(args) < 4 or not is_string(args[0]):
            raise Reject("dropdown args")
        if kind == "Dropdown" and len(args) != 4:
            raise Reject("dropdown categories")
        if kind == "DropdownButton" and len(args) > 7:
            raise Reject("dropdown args")
        v = re.escape(ctx.var)
        # options: Class::Names() declared static const std::vector<std::string>&, or a namespace-scope list
        om = re.fullmatch(r'(\w+)::(\w+)\(\)', args[1])
        if om and options_fn_ok(om.group(1), om.group(2)):
            opts = f"&{om.group(1)}::{om.group(2)}"
        elif re.fullmatch(r'k\w+', args[1]) and re.search(r'^   (?:static |inline )?const std::vector<std::string> ' + args[1] + r'\b', ctx.filesrc, re.M):
            opts = f"&{args[1]}"
        else:
            raise Reject(f"dropdown options {args[1]}")
        # current + store: an int member, or a float param read as (int)(v + 0.5f)
        lam = re.fullmatch(r'\[' + v + r'\]\s*\(int (\w+)\)\s*\{\s*(PushUndoCheckpoint\(\);\s*)?(.*?);\s*\}', args[3], re.S)
        if not lam:
            raise Reject("dropdown onSelect shape")
        iv, undo, store = lam.group(1), lam.group(2) is not None, lam.group(3).strip()
        mm = re.fullmatch(v + r'->(\w+)', args[2])
        if mm:
            if not re.fullmatch(v + r'->' + mm.group(1) + r'\s*=\s*' + iv, store):
                raise Reject("dropdown onSelect does more than store")
            head = f"{alias}::Choice<&{ctx.typ}::{mm.group(1)}>({args[0]}, {opts})"
        else:
            if not re.fullmatch(r'\w+', args[2]):
                raise Reject("dropdown current")
            dm = re.search(r'\bconst int ' + args[2] + r' = \(int\)\(' + v + r'->Param\(("[^"]*")\) \+ 0\.5f\);', ctx.body)
            if not dm or re.search(r'\b' + args[2] + r'\s*=[^=]', ctx.body[dm.end():]):
                raise Reject("dropdown current")
            key = dm.group(1)
            if not re.fullmatch(r'\*' + v + r'->ParamPtr\(' + re.escape(key) + r'\)\s*=\s*\(float\)' + iv, store):
                raise Reject("dropdown onSelect does more than store")
            head = f"{alias}::ChoiceF({args[0]}, []({ctx.typ}* {ctx.var}) {{ return {ctx.var}->ParamPtr({key}); }}, {opts})"
        if undo:
            mods.append(".Undo()")
        extra = args[4:]
        if extra:
            w = ctx.width(extra[0])
            if w != "kParamWidth":
                mods.append(f".Size({ctx.const(w)})")
        if len(extra) > 1:
            if extra[1] == "false":
                mods.append(".NoCaption()")
            elif extra[1] != "true":
                raise Reject("dropdown caption")
        if len(extra) > 2:
            if extra[2] == "true":
                mods.append(".Segmented()")
            elif extra[2] != "false":
                raise Reject("dropdown segmented")
        return head + "".join(mods)

    if kind == "Checkbox":
        if len(args) != 2 or not is_string(args[0]):
            raise Reject("checkbox args")
        return head("Toggle", args[0], args[1], "b", "")

    raise Reject("kind")


_OPT_CACHE = {}


def options_fn_ok(cls, fn):
    key = (cls, fn)
    if key not in _OPT_CACHE:
        pat = re.compile(r'class\s+' + cls + r'\b[^;]*?\{.*?static\s+const\s+std::vector<std::string>\s*&\s*' + fn + r'\s*\(\s*\)', re.S)
        found = False
        for d in ("src/nodes", "src/audio", "src/core", "src/app"):
            for dp, _, fs in os.walk(os.path.join(ROOT, d)):
                for f in fs:
                    if f.endswith(".h"):
                        t = open(os.path.join(dp, f), errors="ignore").read()
                        if ("class " + cls) in t and pat.search(t):
                            found = True
                            break
                if found:
                    break
            if found:
                break
        _OPT_CACHE[key] = found
    return _OPT_CACHE[key]


def func_spans(src):
    for m in FUNC.finditer(src):
        start = m.end()
        if src[start:start + 5] != "\n   {":
            continue
        i = start + 4
        depth = 0
        while i < len(src):
            if src[i] == "{":
                depth += 1
            elif src[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        yield m, start + 1, i + 1


def migrate_text(path, src, report):
    edits = []   # (func_line_start, body_start, body_end, new_body, tables)
    stats = collections.Counter()
    for m, b0, b1 in list(func_spans(src)):
        fname, params_s = m.group(1), m.group(2)
        nm = None
        for pm in NODEARG.finditer(params_s):
            if pm.group(1) not in ("GraphNode", "ImDrawList", "INode", "ImVec2"):
                nm = pm
                break
        if not nm:
            continue
        typ, var = nm.group(1), nm.group(2)
        body = src[b0:b1]
        rowvars = set(re.findall(r'\bAudioKnobRow\s+(\w+)\s*[\(\{]', body))
        ctx = Ctx(typ, var, body, rowvars, src)
        lines = body.split("\n")
        # units: one statement each (a call may span lines), with the line indices it covers
        units, i = [], 0
        while i < len(lines):
            ln = lines[i]
            if CALL_START.match(ln) and not CALL.match(ln):
                j, text = i, ln
                while j + 1 < len(lines) and j - i < 6 and not re.search(r'\);\s*(//.*)?$', text):
                    j += 1
                    text += "\n" + lines[j]
                if CALL.match(text) and text.count("(") == text.count(")") and "//" not in text.rsplit("\n", 1)[0]:
                    units.append((list(range(i, j + 1)), " ".join(t.strip() if k else t for k, t in enumerate(text.split("\n")))))
                    i = j + 1
                    continue
            units.append(([i], ln))
            i += 1
        cls = []
        pend = None  # a `row.index = X;` waiting for the knob-row call after it
        for ids, ln in units:
            cm = CALL.match(ln)
            ce = CELL.match(ln)
            if ce and ce.group(2) in rowvars:
                if pend is not None:
                    cls.append(("other", None, pend[0]))
                try:
                    pend = (ids, ce.group(2), ctx.const(ce.group(3)))
                except Reject:
                    pend = None
                    cls.append(("other", None, ids))
                continue
            if not cm or "\t" in ln:
                if pend is not None:
                    cls.append(("other", None, pend[0])); pend = None
                cls.append(("other" if ln.strip() and not ln.strip().startswith("//") else "gap", None, ids))
                continue
            indent, obj, kind, argstr, comment = cm.groups()
            if kind in ("Knob", "KnobInt", "Dropdown", "Checkbox"):
                if obj is None or obj not in rowvars:
                    if pend is not None:
                        cls.append(("other", None, pend[0])); pend = None
                    cls.append(("other", None, ids)); continue
                group = "knob:" + obj
            else:
                if obj is not None:
                    if pend is not None:
                        cls.append(("other", None, pend[0])); pend = None
                    cls.append(("other", None, ids)); continue
                group = "stack"
            stats["calls"] += 1
            try:
                txt = row_text(ctx, "@", kind, split_args(argstr))
            except Reject as e:
                if pend is not None:
                    cls.append(("other", None, pend[0])); pend = None
                cls.append(("other", None, ids))
                if report:
                    print(f"{path}:{fname}: keep {kind}: {e}")
                stats["reject"] += 1
                continue
            if pend is not None:
                if group == "knob:" + pend[1]:
                    txt += f".Cell({pend[2]})"
                    ids = pend[0] + ids
                else:
                    cls.append(("other", None, pend[0]))
                pend = None
            cls.append(("row", (indent, group, txt, comment), ids))
        if pend is not None:
            cls.append(("other", None, pend[0]))
        # runs of consecutive rows (same group, same indent); comment / blank units may sit inside a run
        runs, cur, last = [], [], None
        for idx, (c, info, _) in enumerate(cls):
            if c == "row":
                if cur and (last[1] != info[1] or last[0] != info[0]):
                    while cur and cls[cur[-1]][0] == "gap":
                        cur.pop()
                    runs.append(cur); cur = []
                cur.append(idx)
                last = info
            elif c == "gap":
                if cur:
                    cur.append(idx)
            else:
                if cur:
                    while cur and cls[cur[-1]][0] == "gap":
                        cur.pop()
                    runs.append(cur); cur = []
        if cur:
            while cur and cls[cur[-1]][0] == "gap":
                cur.pop()
            runs.append(cur)
        runs = [r for r in runs if r]
        if not runs:
            continue
        base = fname[4:] if fname.startswith("Draw") else fname
        alias = base + "Row"
        tables, new_lines, consumed = [], list(lines), set()
        ntab = sum(1 for r in runs if sum(1 for i in r if cls[i][0] == "row") >= 2)
        k = 0
        for r in runs:
            nrows = sum(1 for i in r if cls[i][0] == "row")
            indent, group = cls[r[0]][1][0], cls[r[0]][1][1]
            first_line = cls[r[0]][2][0]
            if nrows == 1:
                # one control: an inline row where the call was
                _, _, txt, comment = cls[r[0]][1]
                txt = txt.replace("@", alias)
                call = (f"Ui::DrawParam({var}, {txt});" if group == "stack" else f"Ui::DrawKnob({group[5:]}, {var}, {txt});")
                new_lines[first_line] = indent + call + (("  " + comment) if comment else "")
                consumed.update(cls[r[0]][2][1:])
                stats["inline"] += 1
                continue
            k += 1
            tname = f"k{base}Rows" + (str(k) if ntab > 1 else "")
            rows = []
            for i in r:
                if cls[i][0] == "gap":
                    t = lines[cls[i][2][0]].strip()
                    rows.append(("         " + t) if t else "")
                    continue
                _, _, txt, comment = cls[i][1]
                rows.append("         " + txt.replace("@", alias) + "," + (("  " + comment) if comment else ""))
                stats["rows"] += 1
            tables.append(f"      const {alias} {tname}[] = {{\n" + "\n".join(rows) + "\n      };")
            draw = (f"{indent}Ui::DrawParams({var}, {tname});" if group == "stack"
                    else f"{indent}Ui::DrawKnobs({group[5:]}, {var}, {tname});")
            new_lines[first_line] = draw
            for i in r:
                consumed.update(cls[i][2])
            consumed.discard(first_line)
        body2 = "\n".join(l for i, l in enumerate(new_lines) if i not in consumed)
        # a local the moved calls were the only readers of is dead now (-Wall -Werror in CI): drop it
        for dm in list(re.finditer(r'^[ \t]*const (?:int|float|bool) (\w+) = [^;\n]+;[ \t]*\n', body2, re.M)):
            if len(re.findall(r'\b' + dm.group(1) + r'\b', body2)) == 1:
                body2 = body2.replace(dm.group(0), "", 1)
                stats["deadlocal"] += 1
        block = ("   namespace\n   {\n" + f"      using {alias} = params::Row<{typ}>;\n" + "\n".join(tables) + ("\n" if tables else "") + "   }\n\n")
        edits.append((m.start(), b0, b1, body2, block))
    if not edits:
        return src, stats
    out = src
    for fstart, b0, b1, body2, block in sorted(edits, reverse=True):
        out = out[:b0] + body2 + out[b1:]
        out = out[:fstart] + block + out[fstart:]
    if INCLUDE not in out:
        out = out.replace('#include "app/AppShared.h"', '#include "app/AppShared.h"\n' + INCLUDE, 1)
        if INCLUDE not in out:
            raise SystemExit(f"{path}: no AppShared.h include to anchor DrawParam.h")
    return out, stats


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry", action="store_true")
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--file", action="append")
    a = ap.parse_args()
    files = a.file or sorted(os.path.join(SCOPE, f) for f in os.listdir(os.path.join(ROOT, SCOPE)) if f.endswith(".cpp"))
    tot = collections.Counter()
    for rel in files:
        p = os.path.join(ROOT, rel)
        src = open(p).read()
        out, st = migrate_text(rel, src, a.report)
        tot.update(st)
        if out != src:
            print(f"{rel}: {st['rows']} rows")
            if not a.dry:
                open(p, "w").write(out)
    print(f"calls seen {tot['calls']}, table rows {tot['rows']}, inline rows {tot['inline']}, kept {tot['reject']}")


if __name__ == "__main__":
    main()
