#!/usr/bin/env python3
"""WCAG contrast check over the role tokens, both themes.

  contrast.py            check (exit 1 if a pair is under its threshold and not listed in contrast_known.json)
  contrast.py --table    print every pair

Text pairs need 4.5:1 (AA). Graphic pairs (pins, curves, status colours, borders) need 3:1 (AA non-text).
Semi-transparent foregrounds are composited over their background first.
Pairs the design accepts below the threshold are listed with a reason in tools/design/contrast_known.json.
"""
import json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
T = json.loads((ROOT / "src/app/ui/design/tokens.json").read_text())
KNOWN = json.loads((ROOT / "tools/design/contrast_known.json").read_text()) if (ROOT / "tools/design/contrast_known.json").exists() else {}
P = T["roles"]["pairs"]
PF = {k: {th: [round(c * 255) for c in v] for th, v in d.items()} for k, d in T["roles"]["pairs_f"].items()}
ALL = {**P, **PF}

# (foreground, background, kind). kind: text -> 4.5, graphic -> 3.0
PAIRS = [
    ("scope.text", "scope.bg", "text"),
    ("curve.value_text", "curvebox.bg", "text"),
    ("curve.value_text", "curvebox.bg_hover", "text"),
    ("dropdown.text", "dropdown.fill", "text"),
    ("dropdown.text", "dropdown.hover", "text"),
    ("dropdown.text", "dropdown.active", "text"),
    ("curve.active", "curvebox.bg", "graphic"),
    ("curve.bent", "curvebox.bg", "graphic"),
    ("curve.idle", "curvebox.bg", "graphic"),
    ("curve.live", "curvebox.bg", "graphic"),
    ("curvebox.border_active", "curvebox.bg", "graphic"),
    ("pin.idle", "pin.well", "graphic"),
    ("pin.mod", "pin.well", "graphic"),
    ("pin.expr", "pin.well", "graphic"),
    ("pin.pred", "pin.well", "graphic"),
    ("pin.cable", "pin.well", "graphic"),
    ("action.record", "backdrop.base", "graphic"),
    ("action.learn", "backdrop.base", "graphic"),
    ("action.go", "backdrop.base", "graphic"),
    ("action.solo", "backdrop.base", "graphic"),
    ("badge.favorite", "backdrop.base", "graphic"),
]

def lin(c):
    c /= 255
    return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4

def lum(rgb):
    return 0.2126 * lin(rgb[0]) + 0.7152 * lin(rgb[1]) + 0.0722 * lin(rgb[2])

def over(fg, bg):
    a = fg[3] / 255
    return [fg[i] * a + bg[i] * (1 - a) for i in range(3)]

def ratio(fg, bg):
    l1, l2 = lum(over(fg, bg)), lum(bg)
    hi, lo = max(l1, l2), min(l1, l2)
    return (hi + 0.05) / (lo + 0.05)

def main():
    bad, rows = [], []
    for fg, bg, kind in PAIRS:
        need = 4.5 if kind == "text" else 3.0
        for th in ("dark", "light"):
            r = ratio(ALL[fg][th], ALL[bg][th])
            key = f"{fg}|{bg}|{th}"
            ok = r >= need
            rows.append((key, kind, r, need, ok))
            if not ok and key not in KNOWN:
                bad.append((key, r, need))
    if "--table" in sys.argv:
        for key, kind, r, need, ok in rows:
            print(f"{'ok  ' if ok else ('known' if key in KNOWN else 'FAIL')} {r:5.2f} (>= {need}) {kind:7s} {key}")
    stale = [k for k in KNOWN if any(k == row[0] and row[4] for row in rows)]
    for k in stale: print("stale known entry (now passes):", k)
    for key, r, need in bad: print(f"CONTRAST FAIL {r:.2f} < {need}: {key}")
    print(f"contrast: {len(rows)} pairs, {len(bad)} failing, {sum(1 for r in rows if not r[4] and r[0] in KNOWN)} known")
    return 1 if bad or stale else 0

if __name__ == "__main__":
    sys.exit(main())
