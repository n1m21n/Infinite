#!/usr/bin/env python3
"""Move the value-named palette (tok::pal::c_RRGGBBAA, tok::palf::v_R_G_B_A) onto role tokens (tok::col::<role>).
Owner sign-off 2026-11-24 on internal/plans/ui-palette-roles.md:

  (a) a group that lands on a fixed role (role.*, state.*) takes that role's colour: a slightly different red
      becomes the one red;
  (b) viz colours keep viz.* names;
  (c) near-duplicate numbered roles fold into their first; deliberate hover / pressed variants get named steps;
  (d) a role token is a dark + light pair: the light value comes from the pair the colour already matches, or is
      computed and checked for contrast against the light backdrop.

How it reads that here:
  - fold (c): `x_N` folds into `x` when the two are within FOLD (OKLab) of each other, FOLD_FIXED for role/state
    (a); otherwise, when most of its uses sit on hover / pressed lines, it becomes `x.hover` / `x.pressed`; else it
    stays a step of its own (`x_N`).
  - fixed: ink.*, role.* and state.* keep one colour in both themes (light = dark), as tokens.json's fixed roles do.
  - a call site that already chooses per theme (`isLight ? A : B`, IsThemeLight() ...) keeps its exact colour:
    it is rewritten to tok::Base / tok::BaseV4 (the role's own value, no theme switch).
  - every other call site becomes tok::Role / tok::RoleV4, which picks dark or light from the current theme.
  - alpha stays at the call site: c_..AA -> Role(col, 0xAA); v_.._A -> RoleV4(col, A / 1000).

Writes roles.col in tokens.json, rewrites src/, drops palette entries nothing uses any more, prints the contrast
check for every computed light value.

    python3 tools/design/palette_migrate.py [--dry]
"""
import json
import math
import os
import re
import subprocess
import sys
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOKENS = os.path.join(ROOT, "src/app/ui/design/tokens.json")
SCAN = ["src/app", "src/core", "src/nodes"]
FOLD = 0.08
FOLD_FIXED = 0.15
CHROMA = 0.03   # a fold never turns a tint grey or a grey into a tint
FIXED_PREFIX = ("ink.", "role.", "state.")
HOVER = re.compile(r"hover|Hover|hov\b|Hov\b|hot\b|Hot\b")
PRESS = re.compile(r"press|Press|active|Active|held|Held|drag|Drag")
THEMED = re.compile(r"[Ll]ight|[Dd]ark|IsThemeLight|\blt\s*\?")


def lin(c):
    c /= 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def unlin(c):
    c = max(0.0, min(1.0, c))
    v = 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055
    return int(round(max(0.0, min(1.0, v)) * 255))


def to_lab(rgb):
    r, g, b = (lin(x) for x in rgb)
    l = (0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b) ** (1 / 3)
    m = (0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b) ** (1 / 3)
    s = (0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b) ** (1 / 3)
    return (0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s)


def from_lab(lab):
    L, a, b = lab
    l = (L + 0.3963377774 * a + 0.2158037573 * b) ** 3
    m = (L - 0.1055613458 * a - 0.0638541728 * b) ** 3
    s = (L - 0.0894841775 * a - 1.2914855480 * b) ** 3
    return [unlin(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s),
            unlin(-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s),
            unlin(-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s)]


def dist(a, b):
    return math.dist(to_lab(a), to_lab(b))


def chroma(rgb):
    _, a, b = to_lab(rgb)
    return math.hypot(a, b)


def lum(rgb):
    return 0.2126 * lin(rgb[0]) + 0.7152 * lin(rgb[1]) + 0.0722 * lin(rgb[2])


def ratio(a, b):
    x, y = lum(a), lum(b)
    return (max(x, y) + 0.05) / (min(x, y) + 0.05)


def hexrgb(h):
    return [int(h[1:3], 16), int(h[3:5], 16), int(h[5:7], 16)]


def member_alpha(m):
    if m.startswith("c_"):
        return int(m[-2:], 16), None
    a = int(m.split("_")[4])
    return None, a / 1000.0


def light_value(rgb, same, pairs, kind, backdrop):
    """(d): the matched pair's light side, else a computed light value with its contrast need."""
    if same and same.startswith("pairs.") and same.endswith(".dark"):
        return pairs[same[len("pairs."):-len(".dark")]]["light"][:3], None
    if same and same.endswith(".light"):
        return rgb, None   # already a light-theme colour
    L, a, b = to_lab(rgb)
    chroma = math.hypot(a, b)
    need = 4.5 if kind == "text" else (3.0 if kind in ("edge", "viz") else None)
    if chroma < 0.04:
        lab = (1.0 - L, a, b)   # neutral: mirror the lightness
    else:
        lab = (L, a, b)          # chromatic: keep the colour, darken only as far as contrast needs
    out = from_lab(lab)
    if need is not None:
        k = lab[0]
        while ratio(out, backdrop) < need and k > 0.05:
            k -= 0.01
            out = from_lab((k, lab[1], lab[2]))
    return out, need


def main():
    dry = "--dry" in sys.argv
    import tempfile
    tmp = os.path.join(tempfile.gettempdir(), "palette_roles.json")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/design/palette_roles.py"), "--out", os.devnull, "--json", tmp],
                   check=True, stdout=subprocess.DEVNULL)
    groups = json.load(open(tmp))
    t = json.load(open(TOKENS, encoding="utf-8"))
    pairs = t["roles"]["pairs"]
    backdrop = pairs["backdrop.base"]["light"][:3]

    # every source line, once, for hover / theme detection
    files = {}
    for base in SCAN:
        for d, _, fs in os.walk(os.path.join(ROOT, base)):
            for f in fs:
                if f.endswith((".cpp", ".h", ".mm")) and f != "Tokens.gen.h":
                    p = os.path.join(d, f)
                    files[p] = open(p, encoding="utf-8").read().split("\n")
    member_lines = defaultdict(list)
    for p, lines in files.items():
        for i, l in enumerate(lines):
            for m in re.findall(r"tok::palf?::((?:c|v)_[0-9A-Fa-f_]+)", l):
                member_lines[m].append(lines[i - 1] + "\n" + l if i else l)

    by_role = {g["role"]: g for g in groups}
    final = {}   # group role -> (token name, rgb)
    for g in groups:
        role, rgb = g["role"], hexrgb(g["hex"])
        m = re.match(r"(.+?)_(\d+)$", role)
        if m and m.group(1) in by_role:
            base = by_role[m.group(1)]
            fixed = role.startswith(FIXED_PREFIX)
            lines = [l for mem in g["members"] for l in member_lines.get(mem, [])]
            hov = sum(1 for l in lines if HOVER.search(l))
            prs = sum(1 for l in lines if PRESS.search(l))
            near = dist(rgb, hexrgb(base["hex"])) < (FOLD_FIXED if fixed else FOLD) and abs(chroma(rgb) - chroma(hexrgb(base["hex"]))) < CHROMA
            if near and max(hov, prs) * 2 <= len(lines):
                final[role] = (m.group(1), hexrgb(base["hex"]))
                continue
            if lines and max(hov, prs) * 2 > len(lines):
                step = m.group(1) + (".hover" if hov >= prs else ".pressed")
                if step not in {n for n, _ in final.values()}:
                    final[role] = (step, rgb)
                    continue
        final[role] = (role, rgb)

    # token table
    col, report = {}, []
    for g in groups:
        name, rgb = final[g["role"]]
        if name in col:
            continue
        kinds = Counter(g["kind"])
        kinds.pop("other", None)
        kind = kinds.most_common(1)[0][0] if kinds else "other"
        if name.startswith(FIXED_PREFIX):
            light, need = rgb, None
        else:
            src = g if final[g["role"]][0] == g["role"] else by_role.get(name, g)
            light, need = light_value(rgb, src.get("same"), pairs, kind, backdrop)
        col[name] = {"dark": rgb + [255], "light": list(light) + [255]}
        if need is not None:
            report.append((name, ratio(light, backdrop), need))

    # rewrite call sites
    m2role = {mem: final[g["role"]][0] for g in groups for mem in g["members"]}
    ident = lambda n: n.replace(".", "_")
    rx = re.compile(r"tok::(U32|V4)\(\s*tok::(pal|palf)::((?:c|v)_[0-9A-Fa-f_]+)\s*(?:,\s*([^(),]+?)\s*)?\)")
    moved = themed = 0
    for p, lines in files.items():
        changed = False
        for i, l in enumerate(lines):
            if "tok::pal" not in l:
                continue
            ctx = (lines[i - 1] if i else "") + "\n" + l + "\n" + (lines[i + 1] if i + 1 < len(lines) else "")

            def sub(mt):
                nonlocal moved, themed
                fn, ns, mem, extra = mt.groups()
                if mem not in m2role:
                    return mt.group(0)
                a8, af = member_alpha(mem)
                explicit = bool(THEMED.search(ctx))
                themed += explicit
                moved += 1
                tokn = f"tok::col::{ident(m2role[mem])}"
                if fn == "U32":
                    alpha = extra if extra else (str(a8) if a8 is not None else str(int(round(af * 255))))
                    return f"tok::{'Base' if explicit else 'Role'}({tokn}, {alpha})"
                if extra:
                    return mt.group(0)
                a = af if af is not None else a8 / 255.0
                return f"tok::{'BaseV4' if explicit else 'RoleV4'}({tokn}, {a:g}f)"

            nl = rx.sub(sub, l)
            if nl != l:
                lines[i] = nl
                changed = True
        if changed and not dry:
            open(p, "w", encoding="utf-8").write("\n".join(lines))

    # tokens.json: add roles.col, drop palette entries nothing uses
    if not dry:
        t["roles"]["col"] = {"_doc": "Palette roles (owner 2026-11-24, internal/plans/ui-palette-roles.md). [dark, light] RGBA 0-255; "
                             "call sites use tok::Role / RoleV4 (theme-switched) or tok::Base / BaseV4 (own value), alpha at the site.",
                             **{k: col[k] for k in sorted(col)}}
        used = set()
        for lines in files.values():
            for l in lines:
                used.update(re.findall(r"tok::palf?::((?:c|v)_[0-9A-Fa-f_]+)", l))
        for grp in ("palette", "palette_f"):
            t[grp] = {k: v for k, v in t[grp].items() if k in used}
        open(TOKENS, "w", encoding="utf-8").write(json.dumps(t, indent=1, ensure_ascii=False) + "\n")

    folded = sum(1 for r, (n, _) in final.items() if n != r)
    print(f"{len(groups)} groups -> {len(col)} role tokens ({folded} folded or renamed); {moved} call sites moved "
          f"({themed} already themed, kept exact)")
    bad = [(n, r, need) for n, r, need in report if r < need - 1e-6]
    for n, r, need in sorted(report):
        print(f"  {'ok  ' if r >= need - 1e-6 else 'FAIL'} {r:5.2f} (>= {need}) {n} light vs backdrop.base")
    print(f"contrast: {len(report)} computed light values checked, {len(bad)} under")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
