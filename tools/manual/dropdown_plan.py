#!/usr/bin/env python3
"""Build tools/manual/dropdown_plan.json: every node dropdown with its option list, from source.

Input: docs/node_param_audit.md (node -> Draw*Params function, controls in order, type, modulatable)
       src/main.cpp + src/app/**/*.cpp (via scripts/appsrc.py)
Output: tools/manual/dropdown_plan.json, one entry per dropdown control:
  {node, class, draw, control, param_index, options, source, status}

param_index is the control's 0-based position among the node's controls in the audit table. It is a
guess at the runtime index used by INFINITE_GALLERYOPENDD and must be checked on one node before a
capture run. status is "resolved" when the options were read from literals, "unresolved" otherwise.

  python3 tools/manual/dropdown_plan.py
"""
import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import appsrc  # noqa: E402

AUDIT = os.path.join(ROOT, "docs", "node_param_audit.md")
OUT = os.path.join(ROOT, "tools", "manual", "dropdown_plan.json")

STR = re.compile(r'"((?:[^"\\]|\\.)*)"')


def parse_audit():
    """Yield (node, class, draw, [(control, type, modulatable)]) per node section."""
    nodes, cur = [], None
    head = re.compile(r"^`(\w+)` · (\w+)\s*$")
    row = re.compile(r"^\| (.+?) \| (\w[\w ]*?) \| (yes|no) \|$")
    title = None
    for line in open(AUDIT, encoding="utf-8"):
        line = line.rstrip("\n")
        if line.startswith("### "):
            title = line[4:].strip()
            cur = None
            continue
        m = head.match(line)
        if m and title:
            cur = {"node": title, "class": m.group(1), "draw": m.group(2), "controls": []}
            nodes.append(cur)
            continue
        m = row.match(line)
        if m and cur is not None and m.group(1) != "control":
            cur["controls"].append((m.group(1), m.group(2), m.group(3)))
    return nodes


def function_bodies(text):
    """Map function name -> (line, body text) for every `Name(...) {` definition in text."""
    out = {}
    for m in re.finditer(r"\b(Draw\w+)\s*\([^;{)]*\)\s*(?:const\s*)?\{", text):
        name = m.group(1)
        if name in out:
            continue
        depth, i = 0, m.end() - 1
        while i < len(text):
            c = text[i]
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        line = text.count("\n", 0, m.start()) + 1
        out[name] = (line, text[m.end() - 1:i + 1])
    return out


def literal_items_from_call(body, label):
    """Find a Combo/BareCombo call whose label mentions `label`; return (items, call_text) or (None, None)."""
    lab = label.lower()
    for m in re.finditer(r"(?:FormParts::)?(?:BareCombo|Combo)\s*\(", body):
        start = m.end()
        depth, i = 1, start
        while i < len(body) and depth:
            if body[i] == "(":
                depth += 1
            elif body[i] == ")":
                depth -= 1
            i += 1
        call = body[start:i - 1]
        first = call.split(",", 1)[0]
        if lab not in first.lower() and lab.replace(" ", "") not in first.lower():
            continue
        # Inline "a\0" "b\0" literals, or Items({ I18N_KEY("a"), ... }) / a plain quoted list.
        strings = STR.findall(call)
        if "\\0" in call and strings:
            items = [s.replace("\\0", "") for s in strings if s not in (first,)]
            items = [s for s in items if s]
            if items:
                return items, call
        if "Items(" in call or "{" in call:
            items = [s for s in strings if s and s != first.strip().strip('"')]
            items = [re.sub(r'^\\"|\\"$', "", s) for s in items]
            if items:
                return items, call
    return None, None


def items_from_array_arg(body, call_text, sources_text):
    """If the call passes a named array (e.g. kModes), read its literal strings from the source."""
    name = re.search(r",\s*(k\w+|\w+Names|\w+Items)\b", call_text)
    if not name:
        return None
    decl = re.search(r"(?:static\s+)?(?:const\s+)?(?:char\*\s*|std::string\s+|const char\*\s*)?"
                     + re.escape(name.group(1)) + r"\s*\[\]\s*=\s*\{([^}]*)\}", sources_text)
    if not decl:
        return None
    return [s for s in STR.findall(decl.group(1)) if s]


def resolve_names(arg, body, text_all):
    """Options for a DropdownButton names argument: a local static vector, a Class::Names() function, or a literal list."""
    arg = arg.strip()
    lit = STR.findall(arg)
    if "{" in arg and lit:
        return lit
    base = re.split(r"\s*\(", arg)[0].strip()
    base = base.split("::")[-1]
    # local declaration in the draw body: static const std::vector<std::string> kAxes = { ... };
    m = re.search(r"\b" + re.escape(base) + r"\s*=\s*\{([^}]*)\}", body)
    if m:
        return [x for x in STR.findall(m.group(1)) if x]
    # function returning the list: `static std::vector<std::string> ModeNames() { return {...}; }` or a const char* array
    m = re.search(r"\b" + re.escape(base) + r"\s*\([^)]*\)\s*(?:const\s*)?\{([^}]*)\}", text_all)
    if m:
        got = [x for x in STR.findall(m.group(1)) if x]
        if got:
            return got
        # `{ return kModeNames; }`: follow the returned array to its literal definition
        ret = re.search(r"return\s+(\w+)\s*;", m.group(1))
        if ret:
            decl = re.search(r"\b" + re.escape(ret.group(1)) + r"\b[^;=]*(?:\[\]\s*)?=?\s*\{([^}]*)\}", text_all)
            if decl:
                return [x for x in STR.findall(decl.group(1)) if x]
    m = re.search(r"\b" + re.escape(base) + r"\s*\[\]\s*=\s*\{([^}]*)\}", text_all)
    if m:
        return [x for x in STR.findall(m.group(1)) if x]
    # namespace-scope constant: inline const std::vector<std::string> kAxisXYZ = { "x", "y", "z" };
    m = re.search(r"\b" + re.escape(base) + r"\s*=\s*\{([^}]*)\}", text_all)
    if m:
        return [x for x in STR.findall(m.group(1)) if x]
    return None


def dropdown_items(body, control, text_all):
    """Options for the DropdownButton("control", names, ...) call in a draw body, or None."""
    for m in re.finditer(r'DropdownButton\s*\(\s*"' + re.escape(control) + r'"\s*,', body):
        start = m.end()
        depth, i = 1, start
        while i < len(body) and depth:
            if body[i] == "(":
                depth += 1
            elif body[i] == ")":
                depth -= 1
            i += 1
        call = body[start:i - 1]
        # the names argument is the first top-level argument after the label
        d, cut = 0, len(call)
        for j, c in enumerate(call):
            if c in "({[":
                d += 1
            elif c in ")}]":
                d -= 1
            elif c == "," and d == 0:
                cut = j
                break
        arg = call[:cut]
        got = resolve_names(arg, body, text_all)
        if got:
            return got
    return None


def main():
    text_all = appsrc.read()
    # Node option lists (ModeNames() and friends) live in src/nodes, which appsrc does not read.
    import glob
    for f in sorted(glob.glob(os.path.join(ROOT, "src", "nodes", "*.cpp"))):
        with open(f, encoding="utf-8", errors="replace") as fh:
            text_all += "\n" + fh.read()
    bodies = function_bodies(text_all)
    nodes = parse_audit()
    plan = []
    stats = {"nodes": len(nodes), "dropdowns": 0, "resolved": 0, "unresolved": 0}
    for n in nodes:
        draw = n["draw"]
        if draw not in bodies:
            continue
        _, body = bodies[draw]
        for idx, (control, ctype, _mod) in enumerate(n["controls"]):
            if ctype != "dropdown":
                continue
            stats["dropdowns"] += 1
            items = dropdown_items(body, control, text_all)
            if items is None:
                items, _ = literal_items_from_call(body, control)
            status = "resolved" if items else "unresolved"
            stats["resolved" if items else "unresolved"] += 1
            plan.append({
                "node": n["node"],
                "class": n["class"],
                "draw": draw,
                "control": control,
                "param_index": idx,
                "options": items or [],
                "source": f"{draw} in src (line {bodies[draw][0]})",
                "status": status,
            })
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump({"stats": stats, "dropdowns": plan}, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print(json.dumps(stats))


if __name__ == "__main__":
    main()
