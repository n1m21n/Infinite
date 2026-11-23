#!/usr/bin/env python3
"""Group the value-named palette colours (tokens.json `palette`, `palette_f`) and propose a role for each group
(chunk 4, internal/plans/ui-hardcode-review.md). Read-only: it writes the proposal table, nothing in src/ moves.

How a group is made: colours whose RGB is within MERGE (OKLab distance) of each other join one group; alpha is kept
apart as a list of alpha variants, since alpha has its own tokens (`alpha.*`). How a role is proposed: from what the
call sites do with the colour (text, fill, edge, viz) plus its lightness and hue. An existing role token close to
the group is named so the group can reuse it instead of adding a new one.

    python3 tools/design/palette_roles.py              # writes internal/plans/ui-palette-roles.md
    python3 tools/design/palette_roles.py --out <path.md> --json out.json
"""
import json
import math
import os
import re
import sys
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOKENS = os.path.join(ROOT, "src/app/ui/design/tokens.json")
SCAN = ["src/app", "src/core", "src/nodes"]
OUT = os.path.join(ROOT, "internal/plans/ui-palette-roles.md")
MERGE = 0.035  # OKLab distance under which two colours read as the same colour

USE = re.compile(r"tok::(pal|palf)::((?:c|v)_[0-9A-Fa-f_]+)")
TEXT = re.compile(r"AddText|TextColored|ImGuiCol_Text\b|Text\(|label|Label|ink|Ink|txt|Txt|fg\b|Fg\b")
EDGE = re.compile(r"AddLine|AddRect\(|AddCircle\(|AddPolyline|AddBezier|Border|border|edge|Edge|stroke|Stroke|outline|Outline|grid|Grid")
FILL = re.compile(r"AddRectFilled|AddCircleFilled|AddConvexPolyFilled|AddTriangleFilled|Bg\b|Bg,|bg\b|Bg\)|Button|Frame|fill|Fill|back|Back")
VIZ = re.compile(r"wave|Wave|meter|Meter|scope|Scope|spectrum|Spectrum|plot|Plot|curve|Curve|graph|Graph|lane|Lane|note|Note|clip|Clip")


def srgb_to_oklab(r, g, b):
    def lin(c):
        c /= 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = lin(r), lin(g), lin(b)
    l = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b
    m = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b
    s = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b
    l, m, s = (x ** (1 / 3) if x > 0 else 0.0 for x in (l, m, s))
    return (0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s)


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def hue_name(lab):
    L, a, b = lab
    c = math.hypot(a, b)
    if c < 0.055:   # the app's greys lean blue (slate): under this chroma they are still greys
        return None
    h = math.degrees(math.atan2(b, a)) % 360
    for lim, name in ((20, "pink"), (40, "red"), (65, "orange"), (90, "amber"), (110, "yellow"), (160, "green"),
                      (200, "teal"), (240, "cyan"), (275, "blue"), (310, "violet"), (345, "magenta"), (360, "pink")):
        if h < lim:
            return name
    return "pink"


# hue -> fixed role from the colour-role table in the infinite-design-system skill (G14), when the use is a fill/mark
HUE_ROLE = {"red": "state.err", "orange": "state.warn", "amber": "role.modulation", "yellow": "role.solo",
            "green": "state.ok", "violet": "role.expression", "blue": "accent", "cyan": "accent.alt",
            "teal": "viz.teal", "magenta": "viz.magenta", "pink": "viz.pink"}


def load():
    t = json.load(open(TOKENS))
    cols = {}
    for k, v in t.get("palette", {}).items():
        if not k.startswith("_"):
            cols[("pal", k)] = tuple(float(x) for x in v[:4])
    for k, v in t.get("palette_f", {}).items():
        if not k.startswith("_"):
            cols[("palf", k)] = tuple(float(x) * 255.0 for x in v[:4])
    roles = {}
    def walk(prefix, node):
        if isinstance(node, dict):
            for k, v in node.items():
                if not k.startswith("_"):
                    walk(f"{prefix}.{k}" if prefix else k, v)
        elif isinstance(node, list) and len(node) in (3, 4) and all(isinstance(x, (int, float)) for x in node):
            scale = 255.0 if all(x <= 1.0 for x in node) else 1.0
            roles[prefix] = tuple(float(x) * scale for x in node[:3])
    walk("", t.get("roles", {}))
    return cols, roles


def scan_uses():
    uses = defaultdict(lambda: {"n": 0, "files": Counter(), "kind": Counter()})
    for d in SCAN:
        for r, _, fs in os.walk(os.path.join(ROOT, d)):
            for f in fs:
                if not f.endswith((".cpp", ".h", ".mm")) or f.endswith(".gen.h"):
                    continue
                p = os.path.join(r, f)
                rel = os.path.relpath(p, ROOT)
                for line in open(p, encoding="utf-8", errors="replace"):
                    for m in USE.finditer(line):
                        u = uses[(m.group(1), m.group(2))]
                        u["n"] += 1
                        u["files"][os.path.basename(rel)] += 1
                        kind = ("text" if TEXT.search(line) else "edge" if EDGE.search(line)
                                else "fill" if FILL.search(line) else "viz" if VIZ.search(line) else "other")
                        u["kind"][kind] += 1
    return uses


def propose(lab, kinds, rgb):
    L = lab[0]
    hue = hue_name(lab)
    named = [k for k, _ in kinds.most_common() if k != "other"]
    kind = named[0] if named else "other"
    if rgb in ((255, 255, 255), (0, 0, 0)):
        return "ink.white" if rgb[0] else "ink.black"   # pure white/black: one fixed ink, the alpha token says how much
    if hue is None:
        level = "hi" if L > 0.85 else "mid" if L > 0.55 else "lo" if L > 0.25 else "deep"
        if kind == "text":
            return {"hi": "text.primary", "mid": "text.secondary", "lo": "text.disabled", "deep": "text.on_light"}[level]
        if kind == "edge":
            return f"edge.{level}"
        return f"surface.{level}"
    if kind == "text":
        return f"text.{HUE_ROLE.get(hue, 'viz.' + hue).split('.')[-1]}"
    return HUE_ROLE.get(hue, "viz." + hue)


def main():
    cols, roles = load()
    uses = scan_uses()
    used = {k: v for k, v in cols.items() if uses.get(k, {}).get("n", 0) > 0}
    # greedy grouping, heaviest colour first so each group is named after its most used member
    order = sorted(used, key=lambda k: -uses[k]["n"])
    labs = {k: srgb_to_oklab(*used[k][:3]) for k in order}
    role_labs = {n: srgb_to_oklab(*c) for n, c in roles.items()}
    groups = []
    for k in order:
        for g in groups:
            if dist(labs[k], g["lab"]) < MERGE:
                g["members"].append(k)
                break
        else:
            groups.append({"lab": labs[k], "head": k, "members": [k]})
    rows = []
    for g in groups:
        n = sum(uses[m]["n"] for m in g["members"])
        files = Counter()
        kinds = Counter()
        alphas = set()
        for m in g["members"]:
            files.update(uses[m]["files"])
            kinds.update(uses[m]["kind"])
            alphas.add(round(used[m][3] / 255.0, 2))
        near = min(role_labs.items(), key=lambda kv: dist(kv[1], g["lab"]), default=(None, None))
        reuse = near[0] if near[0] and dist(near[1], g["lab"]) < MERGE else ""
        r, gg, b = (int(round(x)) for x in used[g["head"]][:3])
        rows.append({"hex": f"#{r:02X}{gg:02X}{b:02X}", "role": propose(g["lab"], kinds, (r, gg, b)), "same": reuse,
                     "members": [m[1] for m in g["members"]], "uses": n, "alphas": sorted(alphas),
                     "kind": dict(kinds), "files": [f for f, _ in files.most_common(3)]})
    rows.sort(key=lambda r: -r["uses"])
    # several groups can land on the same proposed role: number them so each role name is unique
    seen = Counter()
    for r in rows:
        seen[r["role"]] += 1
        if seen[r["role"]] > 1:
            r["role"] = f"{r['role']}_{seen[r['role']]}"
    if "--json" in sys.argv:
        json.dump(rows, open(sys.argv[sys.argv.index("--json") + 1], "w"), indent=1)
    total = sum(r["uses"] for r in rows)
    L = ["# Palette role table (chunk 4, for owner sign-off)", "",
         f"Made by `python3 tools/design/palette_roles.py` from tokens.json `palette` + `palette_f`. "
         f"{len(used)} value-named colours are used ({len(cols) - len(used)} unused, to delete), {total} uses, "
         f"in {len(rows)} groups (colours within OKLab {MERGE} of each other are one group; alpha is kept apart "
         "as a variant). No caller moves until the owner signs off on this table.", "",
         "How to read a row: `role` is the proposed token name, from what the call sites do with the colour (`kind`: "
         "text, fill, edge, viz; `other` is a colour stored in a variable first) and its lightness and hue. "
         "`same colour as` names an existing role token with this colour: the group can point at it when the "
         "meaning matches, but a match in colour alone is not a match in meaning. "
         "To change a role, edit the row and say so; the migration script reads the signed-off table.", "",
         "| # | colour | role | same colour as | uses | members | alphas | kind | main files |",
         "|---|---|---|---|---|---|---|---|---|"]
    for i, r in enumerate(rows, 1):
        role = f"`{r['role']}`"
        same = f"`{r['same']}`" if r["same"] else ""
        kind = ", ".join(f"{k} {v}" for k, v in sorted(r["kind"].items(), key=lambda kv: -kv[1]))
        L.append(f"| {i} | {r['hex']} | {role} | {same} | {r['uses']} | {len(r['members'])} | "
                 f"{', '.join(str(a) for a in r['alphas'])} | {kind} | {', '.join(r['files'])} |")
    L += ["", "Questions for the owner:",
          "1. Groups that land on a fixed role (state.err, role.solo, role.modulation...) take the role's colour, "
          "so a slightly different red becomes the one red. Yes?",
          "2. Viz colours (waveforms, meters, lanes) get `viz.*` names rather than a meaning. Yes?",
          "3. Numbered roles (`surface.lo_2`, `text.secondary_3`) are near-duplicates that miss the merge distance: "
          "fold each into its first, or keep it as its own step?",
          "4. A role token is a dark + light pair. Where `same colour as` names a pair (`pairs.*.dark`), the light "
          "value comes from that pair; the rest get a light value checked by contrast.py. Yes?", ""]
    out = sys.argv[sys.argv.index("--out") + 1] if "--out" in sys.argv else OUT
    os.makedirs(os.path.dirname(out), exist_ok=True)
    open(out, "w").write("\n".join(L))
    print(f"{len(rows)} groups from {len(used)} used colours ({total} uses); wrote {out}")


if __name__ == "__main__":
    main()
