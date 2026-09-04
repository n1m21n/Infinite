#!/usr/bin/env python3
"""Inventory every node's on-screen controls and say which of them a
modulation cable can reach.

Reads src/main.cpp directly rather than driving the app, because spawning all
~136 node types by hand and eyeballing each control is exactly the thing this
exists to avoid. It maps each registered node type to the Draw*Body /
Draw*Params functions that dispatch to it, walks those function bodies, and
classifies every widget call it finds.

    python3 scripts/audit_node_params.py --out docs/node_param_audit.md
"""

import argparse
import os
import re
import sys
from collections import OrderedDict

SRC = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src", "main.cpp")

# Widgets that call Modulation::RegisterParam, so they draw a pin, accept a
# cable and show up in the performance panel's assign picker.
MODULATABLE = OrderedDict([
    ("ModKnob",            "knob"),
    ("ModKnobInt",         "knob (int)"),
    ("ModSlider",          "slider"),
    ("ModSliderInt",       "slider (int)"),
    ("ModCheckbox",        "checkbox"),
    ("DropdownButton",     "dropdown"),
    ("AudioBareDropdown",  "dropdown (bare)"),
    ("ModColorEdit",       "colour"),
    ("row.Knob",           "knob"),
    ("row.KnobInt",        "knob (int)"),
    ("row.Fader",          "fader"),
    ("row.Dropdown",       "dropdown"),
    ("row.DropdownKnob",   "dropdown + knob"),
    ("row.Checkbox",       "checkbox"),
])
# Same widgets reached through a differently-named AudioKnobRow local.
ROW_CALL = re.compile(r"\b(\w*[Rr]ow\w*)\.(Knob|KnobInt|Fader|Dropdown|DropdownKnob|Checkbox)\s*\(")

# Widgets that draw a control but register nothing - no pin, no cable, not
# assignable to a performance-surface element.
PLAIN = OrderedDict([
    ("ImGui::SliderFloat",  "slider"),
    ("ImGui::SliderInt",    "slider (int)"),
    ("ImGui::SliderFloat2", "slider (vec2)"),
    ("ImGui::SliderFloat3", "slider (vec3)"),
    ("ImGui::DragFloat",    "drag"),
    ("ImGui::DragInt",      "drag (int)"),
    ("ImGui::DragFloat2",   "drag (vec2)"),
    ("ImGui::Checkbox",     "checkbox"),
    ("ImGui::Combo",        "combo"),
    ("ImGui::RadioButton",  "radio"),
    ("ImGui::ColorEdit3",   "colour"),
    ("ImGui::ColorEdit4",   "colour"),
    ("ImGui::InputInt",     "input (int)"),
    ("ImGui::InputFloat",   "input"),
    ("ImGui::InputText",    "text"),
    ("ImGui::InputTextWithHint", "text"),
])
# Plain widgets that are deliberately not params - actions, not values.
ACTION_HINT = ("Button", "InputText")

LABEL_ARG = re.compile(r'^\s*(?:&?[\w:>.\-\[\]]+\s*,\s*)?"([^"]*)"')


def read_source():
    with open(SRC, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def function_bodies(text):
    """name -> body source, for every top-level Draw*Body / Draw*Params."""
    out = {}
    for m in re.finditer(r"\n   (?:void|bool)\s+(Draw\w*(?:Body|Params))\s*\(", text):
        name = m.group(1)
        i = text.find("{", m.end())
        if i < 0:
            continue
        depth, j = 0, i
        while j < len(text):
            if text[j] == "{":
                depth += 1
            elif text[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        out[name] = text[i:j]
    return out


def registered_nodes(text):
    """klass -> (display name, category), in registration order."""
    out = OrderedDict()
    for m in re.finditer(r"REGISTER_NODE\((\w+),\s*([^,]+?),\s*\"([^\"]*)\"\)", text):
        out[m.group(1)] = (m.group(2).strip(), m.group(3))
    return out


def dispatch_map(text):
    """klass -> set of Draw* functions it is dispatched to."""
    out = {}
    lines = text.split("\n")
    for i, line in enumerate(lines):
        m = re.search(r"dynamic_cast<(\w+)\s*\*>", line)
        if not m:
            continue
        klass = m.group(1)
        window = "\n".join(lines[i:i + 4])
        for fn in re.findall(r"\b(Draw\w*(?:Body|Params))\s*\(", window):
            out.setdefault(klass, set()).add(fn)
    return out


def label_of(body, pos):
    m = LABEL_ARG.match(body[pos:pos + 220])
    if not m:
        return "?"
    return m.group(1).split("##")[0].strip() or "?"


def scan(body):
    """[(label, widget kind, modulatable?)] for one function body."""
    found = []
    seen = set()

    def add(pos, label, kind, mod):
        key = (pos,)
        if key in seen:
            return
        seen.add(key)
        found.append((pos, label, kind, mod))

    for call, kind in MODULATABLE.items():
        if call.startswith("row."):
            continue
        for m in re.finditer(r"(?<![\w:.])" + re.escape(call) + r"\s*\(", body):
            add(m.start(), label_of(body, m.end()), kind, True)
    for m in ROW_CALL.finditer(body):
        kind = {"Knob": "knob", "KnobInt": "knob (int)", "Fader": "fader",
                "Dropdown": "dropdown", "DropdownKnob": "dropdown + knob",
                "Checkbox": "checkbox"}[m.group(2)]
        add(m.start(), label_of(body, m.end()), kind, True)
    for call, kind in PLAIN.items():
        for m in re.finditer(r"(?<![\w:.])" + re.escape(call) + r"\s*\(", body):
            add(m.start(), label_of(body, m.end()), kind, False)

    found.sort()
    return [(lbl, kind, mod) for _, lbl, kind, mod in found]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="docs/node_param_audit.md")
    args = ap.parse_args()

    text = read_source()
    bodies = function_bodies(text)
    nodes = registered_nodes(text)
    dispatch = dispatch_map(text)

    rows, total_mod, total_plain, no_draw = [], 0, 0, []
    for klass, (display, category) in nodes.items():
        fns = sorted(dispatch.get(klass, []))
        controls = []
        for fn in fns:
            if fn not in bodies:
                continue
            for lbl, kind, mod in scan(bodies[fn]):
                controls.append((lbl, kind, mod, fn))
        if not fns:
            no_draw.append((category, display, klass))
        for _, _, mod, _ in controls:
            if mod:
                total_mod += 1
            else:
                total_plain += 1
        rows.append((category, display, klass, fns, controls))

    rows.sort(key=lambda r: (r[0], r[1]))

    out = []
    out.append("# Node parameter audit\n")
    out.append("Generated by `scripts/audit_node_params.py` from `src/main.cpp`. "
               "Re-run it after touching any `Draw*Body` / `Draw*Params` function.\n")
    out.append("A control is **modulatable** when its widget registers a `ParamRef` "
               "(`Modulation::RegisterParam`). That is the same thing three times over: "
               "it draws a pin, a cable can be dropped on it, and the performance "
               "panel's *Assign Parameter* picker can see it.\n")
    out.append(f"- {len(rows)} registered node types\n"
               f"- {total_mod} modulatable controls\n"
               f"- {total_plain} plain controls (no pin, no cable, not assignable)\n")
    if no_draw:
        out.append("\n## Node types with no Draw* function found\n")
        out.append("These draw through a shared/generic path, so this script cannot "
                   "attribute controls to them.\n")
        for cat, display, klass in sorted(no_draw):
            out.append(f"- {cat} / **{display}** (`{klass}`)")
        out.append("")

    current_cat = None
    for cat, display, klass, fns, controls in rows:
        if cat != current_cat:
            out.append(f"\n## {cat or '(uncategorised)'}\n")
            current_cat = cat
        gaps = [c for c in controls if not c[2] and c[1] not in ("text",)]
        flag = "" if not gaps else f"  —  **{len(gaps)} not modulatable**"
        out.append(f"### {display}{flag}\n")
        out.append(f"`{klass}`" + (f" · {', '.join(fns)}" if fns else " · _no Draw* function found_") + "\n")
        if not controls:
            out.append("_no controls detected_\n")
            continue
        out.append("| control | type | modulatable |")
        out.append("| --- | --- | --- |")
        for lbl, kind, mod in [(c[0], c[1], c[2]) for c in controls]:
            out.append(f"| {lbl} | {kind} | {'yes' if mod else 'NO'} |")
        out.append("")

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print(f"wrote {args.out}: {len(rows)} nodes, {total_mod} modulatable, {total_plain} plain")


if __name__ == "__main__":
    sys.exit(main())
