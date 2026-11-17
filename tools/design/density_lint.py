#!/usr/bin/env python3
"""Panel density lint (internal/plans/ui-system/density.md).

Reads a ui-audit registry (every control's box, per panel and state) and reports, per panel window:
  row pitch       median distance between control rows (rule: <= 26)
  per row         controls per row in settings panels (rule: >= 1.6)
  height          median control height (rule: <= 24)

    python3 tools/design/density_lint.py [ui-audit-view2d/registry.json] [--strict]

Where the registry comes from: it is NOT committed and NOT derived from source. The UI audit writes it
when it runs the 2D View against a debug app:

    python3 scripts/ibuild.py build --mode dev            # builds build-dev/Infinite.app (needs a machine that can build)
    python3 tools/ui-audit/run.py --view2d                 # writes ui-audit-view2d/registry.json + coverage.tsv

Without a registry the script exits 2 and says so; it never invents rows. ui-audit-view2d/ is gitignored.

--strict exits 1 on any failing panel (for the pre-push gate once block 3 lands).
"""
import collections
import json
import statistics as st
import sys

PITCH_MAX, PER_ROW_MIN, H_MAX = 26.0, 1.6, 24.0
CANVAS = ("content",)   # a canvas window: overlays sit anywhere, so row pitch is not measured
SETTINGS = ("props", "more", "popup", "##v2dlayers")   # panels that hold settings, not tool strips or tab rails


def rows_of(boxes):
    """Groups boxes into rows by vertical centre (a box joins a row whose centre is within 6 px)."""
    rows = []
    for b in sorted(boxes, key=lambda b: (b[1] + b[3]) / 2):
        c = (b[1] + b[3]) / 2
        if rows and abs(rows[-1][0] - c) <= 6:
            rows[-1][1].append(b)
        else:
            rows.append([c, [b]])
    return rows


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    strict = "--strict" in sys.argv
    path = args[0] if args else "ui-audit-view2d/registry.json"
    try:
        reg = json.load(open(path))
    except FileNotFoundError:
        print(f"density_lint: no registry at {path}. Produce it with: "
              "python3 scripts/ibuild.py build --mode dev && python3 tools/ui-audit/run.py --view2d",
              file=sys.stderr)
        return 2
    groups = collections.defaultdict(list)
    for c in reg:
        b = c.get("bb")
        if not b or b[3] - b[1] > 60 or b[2] - b[0] > 900:   # grips, canvases, whole bars
            continue
        groups[(c["window"], c.get("state", ""))].append(b)

    per_win = collections.defaultdict(lambda: {"pitch": [], "per_row": [], "h": []})
    for (win, _state), boxes in groups.items():
        rows = rows_of(boxes)
        cs = [r[0] for r in rows]
        if len(cs) > 1:
            per_win[win]["pitch"].append(st.median([b - a for a, b in zip(cs, cs[1:])]))
        per_win[win]["per_row"].append(len(boxes) / len(rows))
        per_win[win]["h"].append(st.median([b[3] - b[1] for b in boxes]))

    bad = 0
    print(f"{'panel':<26}{'pitch':>7}{'per row':>9}{'height':>8}  verdict")
    for win, m in sorted(per_win.items()):
        pitch = st.median(m["pitch"]) if m["pitch"] else 0.0
        per_row = st.median(m["per_row"])
        h = st.median(m["h"])
        fails = []
        if pitch > PITCH_MAX and not any(k in win for k in CANVAS):
            fails.append(f"pitch>{PITCH_MAX:.0f}")
        if h > H_MAX:
            fails.append(f"height>{H_MAX:.0f}")
        if any(k in win for k in SETTINGS) and per_row < PER_ROW_MIN:
            fails.append(f"per row<{PER_ROW_MIN}")
        bad += bool(fails)
        print(f"{win[:25]:<26}{pitch:>7.1f}{per_row:>9.1f}{h:>8.1f}  {'ok' if not fails else ', '.join(fails)}")
    print(f"\n{bad} of {len(per_win)} panels off the density rules")
    return 1 if strict and bad else 0


if __name__ == "__main__":
    sys.exit(main())
