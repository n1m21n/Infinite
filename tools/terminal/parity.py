#!/usr/bin/env python3
"""Every app action a person can click should have an `infinite` verb, or a stated reason not to.

Scans labelled menu items, buttons and settings controls in src/app and checks each against
tools/terminal/parity.json: {"verbs": {"file or file:label": "verb"}, "skip": {"file or file:label": "why"}}.
A file-wide entry covers every label in that file. Exit 1 when something is uncovered.

  parity.py            uncovered actions, by file
  parity.py --summary  counts only
"""
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WIDGET = re.compile(
    r'\b(?:MenuParts::(?:Item|Check|Choice|SubMenu)|ImGui::(?:MenuItem|Button|SmallButton|Checkbox|Selectable)'
    r'|FormParts::\w+|\w*Button\w*)\(\s*(?:L\(|I18N_KEY\()?"([^"#][^"]*)"')


def actions():
    found = defaultdict(set)
    for path in sorted((ROOT / "src/app").rglob("*.cpp")) + sorted((ROOT / "src/app").rglob("*.h")):
        rel = path.relative_to(ROOT / "src/app").as_posix()
        if "selftest" in rel or "/design/" in rel or re.search(r"(Tests|Bench)", path.name):
            continue
        text = path.read_text(errors="replace")
        for m in WIDGET.finditer(text):
            label = m.group(1).split("##")[0].strip()
            if label and re.search("[A-Za-z]", label):
                found[rel].add(label)
    return found


def main():
    table = json.loads((ROOT / "tools/terminal/parity.json").read_text())
    covered = {**table["verbs"], **table["skip"]}
    open_items, total = defaultdict(list), 0
    for rel, labels in actions().items():
        for label in sorted(labels):
            total += 1
            if rel not in covered and f"{rel}:{label}" not in covered:
                open_items[rel].append(label)
    missing = sum(len(v) for v in open_items.values())
    if "--summary" not in sys.argv:
        for rel, labels in sorted(open_items.items(), key=lambda kv: -len(kv[1])):
            print(f"{rel} ({len(labels)})")
            for label in labels:
                print(f"  {label}")
    else:
        for rel, labels in sorted(open_items.items(), key=lambda kv: -len(kv[1])):
            print(f"{len(labels):4} {rel}")
    print(f"parity: {total - missing} of {total} covered, {missing} open")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
