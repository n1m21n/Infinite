#!/usr/bin/env python3
"""Move every control range ui_audit.py lists (`param_range`, `slider`) into a parameter table (owner 2026-11-24):

  one table per node, in src/app/graph/NodeParams.h beside NodeRegistry.cpp, in its registration order and named for
  the node class (params::AudioFile for AudioFileNode); a panel's own setting ranges get one table per panel
  (params::view2d). The literal moves exactly as written (its text, so its type and value), so nothing a patch saves
  changes: param names, order, defaults, ranges and steps stay identical. canvas_pos stays with the patch data.

  ModSlider("gain", &n->gain, -60.0f, 12.0f)  ->  ModSlider("gain", &n->gain, params::AudioFile::gain_lo, params::AudioFile::gain_hi)

The node is the class of the variable the control writes (`n` in &n->gain: FooNode* n, auto* n = dynamic_cast<FooNode*>);
a control on anything else belongs to its panel. A literal that is not a whole argument is printed and left.

    python3 tools/design/param_tables.py [--dry]
"""
import os, re, sys
from collections import OrderedDict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ui_audit  # noqa: E402
import size_migrate as sm  # noqa: E402

ROOT = ui_audit.ROOT
OUT = ROOT / "src/app/graph/NodeParams.h"
REG = ROOT / "src/app/graph/NodeRegistry.cpp"
INC = '#include "app/graph/NodeParams.h"'
SPEED_FIRST = {"DragRow", "DragFloat", "DragFloat2", "DragFloat3", "DragFloat4", "DragInt", "DragMulti", "DragFloatRange2"}
ROLES = ["lo", "hi", "def", "step"]
LOCAL = {"v", "val", "value", "tmp", "f", "x", "y", "z", "t", "d", "i", "k", "s", "fv", "iv"}


def main():
    dry = "--dry" in sys.argv
    reg_src = REG.read_text()
    reg = OrderedDict()
    for m in re.finditer(r"REGISTER_NODE\((\w+),\s*([^,]+),", reg_src):
        reg.setdefault(m.group(1), m.group(2).strip())

    # existing tables (a rerun adds to them)
    tables = OrderedDict()   # ns -> OrderedDict(name -> (type, text))
    heads = {}
    if OUT.exists():
        ns = None
        for line in OUT.read_text().split("\n"):
            m = re.match(r"\s*namespace (\w+)$", line)
            if m and m.group(1) != "params": ns = m.group(1); tables.setdefault(ns, OrderedDict()); continue
            m = re.match(r"\s*constexpr (\w+) (\w+) = (.+);$", line)
            if m and ns: tables[ns][m.group(2)] = (m.group(1), m.group(3))
            m = re.match(r"\s*// (.+) \(REGISTER_NODE (\w+)\)$", line)
            if m: heads[m.group(2)] = m.group(1)

    findings, _, _ = ui_audit.run()
    by_file = {}
    for f in findings:
        if f[0] in ("param_range", "slider") and not f[4].startswith(ui_audit.KIT):
            by_file.setdefault(f[4], []).append(f)

    node_of_ns = {}
    done = skipped = 0
    for rel, fs in sorted(by_file.items()):
        path = ROOT / rel
        src = path.read_text(encoding="utf-8")
        lines = src.split("\n")
        ctx = sm.Ctx(src)
        T = ctx.toks
        surf = sm.surface_of(rel)
        by_line = {}
        for i, t in enumerate(T):
            if t.kind == "num": by_line.setdefault(t.line, []).append(i)
        used, edits = set(), []

        def close_of(k):
            d = 0
            for j in range(k, len(T)):
                if T[j].text in "([{": d += 1
                elif T[j].text in ")]}":
                    d -= 1
                    if d == 0: return j
            return None

        def args_of(k, e):
            out, d, a = [], 0, k + 1
            for j in range(k + 1, e):
                t = T[j].text
                if t in "([{": d += 1
                elif t in ")]}": d -= 1
                elif t == "," and d == 0: out.append((a, j)); a = j + 1
            out.append((a, e))
            return out

        def node_class(var, ln):
            # nearest declaration of `var` at or above line ln: `FooNode* var`, `var = dynamic_cast<FooNode*>`
            pat1 = re.compile(rf"\b(\w+)\s*\*\s*(?:const\s+)?{re.escape(var)}\b")
            pat2 = re.compile(rf"\b{re.escape(var)}\s*=\s*(?:dynamic_cast|static_cast|reinterpret_cast)<\s*(?:const\s+)?(\w+)\s*\*")
            pat3 = re.compile(rf"\b(\w+)\s*&\s*{re.escape(var)}\b")
            for j in range(ln - 1, max(-1, ln - 1500), -1):
                L = lines[j]
                for p in (pat2, pat1, pat3):
                    m = p.search(L)
                    if m and m.group(1) in reg: return m.group(1)
                    if m and m.group(1) not in ("auto", "const", "char", "float", "int", "double", "bool", "void"): return None
            return None

        for f in fs:
            cat, call, text, stmt, _, ln, func = f
            body = text.lstrip("-").strip()
            cand = [i for i in by_line.get(ln, []) if T[i].text == body and i not in used]
            pick = None
            for i in cand:
                st = ctx.info.get(i, [])
                if not st: continue
                top = st[-1]
                if top[0] != call.split("::")[-1] or top[1] != "(": continue
                k = top[3]
                e = close_of(k)
                if e is None: continue
                spans = args_of(k, e)
                a, b = spans[top[2]]
                neg = b - a == 2 and T[a].text == "-"
                if not (b - a == 1 or neg): continue
                pick = (i, k, e, spans, top[2], neg)
                break
            if pick is None and any(T[i].text == body and i in used for i in by_line.get(ln, [])):
                continue   # moved with its partner (the other end of the range)
            if pick is None:
                print(f"skip {rel}:{ln} {text} in {call}: not a whole argument"); skipped += 1; continue
            i, k, e, spans, ai, neg = pick
            if T[k - 2].text in ("bool", "void", "float", "int") or T[k - 2].kind == "id" and T[k - 3].text in ("bool", "void", "int", "float"):
                print(f"skip {rel}:{ln} {call} declaration default"); skipped += 1; used.add(i); continue
            # the value slot: the pointer argument, else the label
            ptr = next((x for x in spans if T[x[0]].text == "&"), None)
            name = ""
            head = None
            if ptr:
                ids, nums, depth = [], [], 0
                for j in range(ptr[0], ptr[1]):   # an index variable (lane in [lane]) is not the param's name
                    if T[j].text == "[": depth += 1
                    elif T[j].text == "]": depth -= 1
                    elif T[j].kind == "id" and depth == 0: ids.append(T[j].text)
                    elif T[j].kind == "num": nums.append(T[j].text)
                if ids:
                    head = ids[0]
                    leaf = ids[-1]
                    if leaf not in LOCAL or len(ids) > 1:
                        name = sm.snake(leaf) + "".join("_" + x for x in nums)
            if not name:
                lab = next((T[j].text for j in range(spans[0][0], spans[0][1]) if T[j].kind == "str"), None) if spans else None
                if lab:
                    name = sm.snake(re.sub(r"##.*", "", lab.strip('"')).strip()) or sm.snake(lab.strip('"#'))
            if not name:
                name = sm.func_part(func, surf) or "value"
            name = re.sub(r"_+", "_", name).strip("_") or "value"
            if name[0].isdigit(): name = "p" + name
            # role of this argument among the numeric ones after the pointer
            start = spans.index(ptr) + 1 if ptr else 1
            numeric = [x for x in range(start, len(spans)) if spans[x][1] - spans[x][0] in (1, 2) and
                       T[spans[x][1] - 1].kind == "num"]
            roles = (["speed"] + ROLES) if call.split("::")[-1] in SPEED_FIRST else ROLES
            pos = numeric.index(ai) if ai in numeric else 0
            role = roles[pos] if pos < len(roles) else f"arg{pos}"
            cls = node_class(head, ln) if head else None
            if cls is None and func and func != "-":
                m = re.search(rf"\b{re.escape(func)}\s*\(\s*(?:const\s+)?(\w+)\s*\*", src)
                if m and m.group(1) in reg and head in ("n", "node", "self", "nd"): cls = m.group(1)
            ns = re.sub(r"Node$", "", cls) if cls else surf
            if cls: node_of_ns[ns] = cls
            tab = tables.setdefault(ns, OrderedDict())
            lit = ("-" if neg else "") + T[i].text
            ty = {"int": "int", "float": "float", "double": "double"}[sm.num_kind(T[i].text)]
            # a complete range: the partner of a lo / hi moves with it
            n2 = 1
            while True:
                key = f"{name}_{role}" if n2 == 1 else f"{name}_{n2}_{role}"
                if key not in tab or tab[key] == (ty, lit): break
                n2 += 1
            tab[key] = (ty, lit)
            used.add(i)
            edits.append((T[spans[ai][0]].pos, T[i].pos + len(T[i].text), f"params::{ns}::{key}"))
            done += 1
            if role in ("lo", "hi"):
                other = "hi" if role == "lo" else "lo"
                oi = pos + (1 if role == "lo" else -1)
                if 0 <= oi < len(numeric):
                    x = numeric[oi]
                    a, b = spans[x]
                    j = b - 1
                    if j not in used and T[j].kind == "num":
                        olit = ("-" if b - a == 2 else "") + T[j].text
                        oty = {"int": "int", "float": "float", "double": "double"}[sm.num_kind(T[j].text)]
                        okey = f"{name}_{other}" if n2 == 1 else f"{name}_{n2}_{other}"
                        if okey not in tab or tab[okey] == (oty, olit):
                            tab[okey] = (oty, olit)
                            used.add(j)
                            edits.append((T[a].pos, T[j].pos + len(T[j].text), f"params::{ns}::{okey}"))
        if not edits: continue
        seen = set()
        for a, b, t in sorted(edits, reverse=True):
            if (a, b) in seen: continue
            seen.add((a, b))
            src = src[:a] + t + src[b:]
        if INC not in src:
            m = re.search(r"^#include .*$", src, re.M) or re.search(r"^#pragma once.*$", src, re.M)
            src = (src[:m.end()] + "\n" + INC + src[m.end():]) if m else INC + "\n" + src
        if not dry: path.write_text(src, encoding="utf-8")

    # write the header: nodes in registration order, then panels
    for ns, cls in node_of_ns.items(): heads.setdefault(cls, reg.get(cls, cls))
    order = {re.sub(r"Node$", "", c): n for n, c in enumerate(reg)}
    nodes = sorted((ns for ns in tables if ns in order), key=lambda s: order[s])
    panels = sorted(ns for ns in tables if ns not in order)
    L = ["// Control ranges (owner 2026-11-24, tools/design/param_tables.py made the first cut; edit by hand from here).",
         "// One table per node, in NodeRegistry.cpp's registration order, named for the node class; then one table per",
         "// panel for its own settings. A value here is only the control's range (lo / hi), drag speed, default or step:",
         "// saved patches, param names, order and defaults do not read it, so changing nothing here changes no data.",
         "#pragma once", "", "namespace params", "{"]
    for ns in nodes:
        cls = next(c for c in reg if re.sub(r"Node$", "", c) == ns)
        L += [f"   // {reg[cls]} (REGISTER_NODE {cls})", f"   namespace {ns}", "   {"]
        L += [f"      constexpr {ty} {k} = {v};" for k, (ty, v) in tables[ns].items()]
        L += ["   }", ""]
    if panels:
        L += ["   // Panel and settings controls (not node params)"]
    for ns in panels:
        L += [f"   namespace {ns}", "   {"]
        L += [f"      constexpr {ty} {k} = {v};" for k, (ty, v) in tables[ns].items()]
        L += ["   }", ""]
    L += ["}", ""]
    if not dry: OUT.write_text("\n".join(L))
    print(f"param_tables: {done} moved, {skipped} skipped; {len(nodes)} node tables, {len(panels)} panel tables, "
          f"{sum(len(t) for t in tables.values())} entries")


if __name__ == "__main__":
    main()
