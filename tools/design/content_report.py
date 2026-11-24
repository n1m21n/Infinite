#!/usr/bin/env python3
"""Every declared `// ui-content-begin: <reason>` region, with its reason and what it would have counted.

    python3 tools/design/content_report.py            # table: file:line, reason, hits hidden by the region
    python3 tools/design/content_report.py --check    # exit 1 on an unclosed region or an empty reason

Reviewed like the allowed buckets (internal/plans/ui-violator-cleanup.md section 7): a region passes only with a
named reason, and the owner spot-checks them. A region that hides chrome (a button, a card, a label) is a bug.
"""
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import kit  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
HITS = {
    "shapes": re.compile(r"->Add(?:Rect|RectFilled|Circle|CircleFilled|Line|Triangle|TriangleFilled|Quad|QuadFilled|Bezier\w*"
                         r"|Polyline|ConvexPolyFilled)\("),
    "text": re.compile(r"->AddText\("),
    "layout": re.compile(r"\bImGui::(?:SameLine|SetCursorPos|SetCursorPosX|SetCursorPosY|Dummy|GetContentRegionAvail)\s*\("),
    # raw ImGui chrome inside a content region is the thing this report exists to catch (kit parts are not violations,
    # and input checks such as MenuParts::RightPressed are not drawing)
    "widgets": re.compile(r"ImGui::(?:Button|SmallButton|ArrowButton|Checkbox|RadioButton|BeginCombo|Combo|Selectable|InputText\w*"
                          r"|Slider\w+|Drag\w+|BeginMenu|MenuItem|BeginPopup\w*|BeginChild|BeginTable|ColorEdit\d|ColorButton"
                          r"|CollapsingHeader|TreeNode\w*)\("),
}


def regions():
    for base in ("src/app", "src/arrange", "src/nodes"):
        for f in sorted((ROOT / base).rglob("*")):
            if f.suffix not in (".cpp", ".h", ".mm"):
                continue
            rel = str(f.relative_to(ROOT))
            for line, reason, body in kit.content_regions(f.read_text(errors="ignore")):
                yield rel, line, reason, body


def per_file():
    """{file: region count} for the ratchet"""
    out = {}
    for rel, *_ in regions():
        out[rel] = out.get(rel, 0) + 1
    return out


def main():
    bad, rows = [], []
    for rel, line, reason, body in regions():
        if body is None:
            bad.append(f"{rel}:{line}: ui-content-begin without ui-content-end")
            continue
        if len(reason) < 8:
            bad.append(f"{rel}:{line}: reason too short: '{reason}'")
        n = {k: len(rx.findall(body)) for k, rx in HITS.items()}
        if n["widgets"]:
            bad.append(f"{rel}:{line}: {n['widgets']} widget calls inside a content region (chrome is not content)")
        rows.append((rel, line, reason, n))
    if "--check" not in sys.argv:
        print("| where | reason | shapes | text | layout |\n|---|---|---|---|---|")
        for rel, line, reason, n in rows:
            print(f"| {rel}:{line} | {reason} | {n['shapes']} | {n['text']} | {n['layout']} |")
        tot = {k: sum(r[3][k] for r in rows) for k in HITS}
        print(f"\n{len(rows)} regions in {len({r[0] for r in rows})} files; hidden: {tot['shapes']} shapes, {tot['text']} text, "
              f"{tot['layout']} layout calls")
    for b in bad:
        print("CONTENT: " + b)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
