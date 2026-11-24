#!/usr/bin/env python3
"""Name the numbered colour roles in tokens.json `col` (accent_2, state_ok_11, ...) for how they differ from their base
role, in OKLab: lightness (pale / light / dark / deep), chroma (vivid / muted), hue (warm / cool), and `fixed` when the
base switches with the theme and this one does not. A step that differs from its base in none of these is `alt`.

    accent_2 -> accent_light   state_ok_11 -> state_ok_deep   role_expression_12 -> role_expression_deep_muted

Renames the key in tokens.json and every tok::col::<old> in src (rerun build_tokens.py after).

    python3 tools/design/colour_role_names.py [--dry]
"""
import json, math, os, re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOK = ROOT / "src/app/ui/design/tokens.json"


def oklab(c):
    def lin(x):
        x /= 255.0
        return x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4
    r, g, b = (lin(v) for v in c[:3])
    l = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b
    m = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b
    s = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b
    l, m, s = (math.copysign(abs(v) ** (1 / 3), v) for v in (l, m, s))
    L = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s
    A = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s
    B = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s
    return L, math.hypot(A, B), math.degrees(math.atan2(B, A))


def words_for(c, base):
    L, C, H = oklab(c["dark"])
    L0, C0, H0 = oklab(base["dark"])
    out = []
    dl = L - L0
    out.append("pale" if dl > 0.15 else "light" if dl > 0.05 else "deep" if dl < -0.15 else "dark" if dl < -0.05 else "")
    dc = C - C0
    out.append("vivid" if dc > 0.04 else "muted" if dc < -0.04 else "")
    dh = (H - H0 + 180) % 360 - 180
    out.append("" if C < 0.03 or C0 < 0.03 or abs(dh) < 15 else ("warm" if dh < 0 else "cool"))
    if base["dark"] != base["light"] and c["dark"] == c["light"]: out.append("fixed")
    return [w for w in out if w]


def main():
    dry = "--dry" in sys.argv
    d = json.loads(TOK.read_text())
    col = d["roles"]["col"]
    keys = [k for k in col if not k.startswith("_")]
    rename = {}
    taken = set(k for k in keys if not re.search(r"_\d+$", k))
    groups = {}
    for k in (k for k in keys if re.search(r"_\d+$", k)):
        root = re.sub(r"_\d+$", "", k)
        if root not in col: print(f"no base for {k}"); continue
        ws = words_for(col[k], col[root]) or ["alt"]
        groups.setdefault(root + "_" + "_".join(ws), []).append(k)
    # several steps with the same relation to their base: a lightness ladder (darkest first)
    LADDER = {2: ["lo", "hi"], 3: ["lo", "mid", "hi"], 4: ["xlo", "lo", "hi", "xhi"], 5: ["xlo", "lo", "mid", "hi", "xhi"]}
    for nm, ks in groups.items():
        if len(ks) == 1 and nm not in taken:
            rename[ks[0]] = nm; taken.add(nm); continue
        ks.sort(key=lambda k: oklab(col[k]["dark"])[0])
        steps = LADDER.get(len(ks)) or [chr(97 + i) for i in range(len(ks))]
        for k, st in zip(ks, steps):
            rename[k] = nm + "_" + st; taken.add(rename[k])
    for o, n in rename.items(): print(f"{o} -> {n}")
    if dry: return 0
    t = TOK.read_text()
    for o, n in rename.items():
        assert t.count(f'"{o}":') == 1, o
        t = t.replace(f'"{o}":', f'"{n}":')
    TOK.write_text(t)
    ident = lambda k: k.replace(".", "_")
    ren = {ident(o): ident(n) for o, n in rename.items()}
    rx = re.compile(r"\btok::col::(" + "|".join(sorted(map(re.escape, ren), key=len, reverse=True)) + r")\b")
    n = 0
    for dd, _, fs in os.walk(ROOT / "src"):
        for f in fs:
            if not f.endswith((".h", ".cpp", ".mm")) or f.endswith(".gen.h"): continue
            p = os.path.join(dd, f)
            s = open(p, encoding="utf-8").read()
            s2 = rx.sub(lambda m: "tok::col::" + ren[m.group(1)], s)
            if s2 != s: open(p, "w", encoding="utf-8").write(s2); n += 1
    print(f"colour_role_names: {len(rename)} roles renamed in {n} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
