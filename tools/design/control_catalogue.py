#!/usr/bin/env python3
"""Print the control catalogue table: one row per control type, from ratchet.py's own rules.

Each row: type, its one shared kit part (or "none yet"), raw copies outside src/app/ui/design (files, uses),
the ratchet rule that only lets that count go down, and the stored baseline. Usage:
  python3 tools/design/control_catalogue.py            # markdown table to stdout
"""
import json, pathlib, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import ratchet  # noqa: E402

def main():
    stored = json.loads(ratchet.STORE.read_text()).get("lints", {}) if ratchet.STORE.exists() else {}
    print("| type | kit part | files | uses | ratchet rule | stored |")
    print("|---|---|---|---|---|---|")
    for name, (rx, part) in ratchet.CONTROLS.items():
        rx, allow, bases, _ = ratchet.LINTS["ctl_" + name]
        cur = ratchet.lint_counts(rx, allow, bases)
        print(f"| {name.replace('_', ' ')} | {part} | {len(cur)} | {sum(cur.values())} | `ctl_{name}` | {sum(stored.get('ctl_' + name, {}).values())} |")
    print()
    print("| other lint | files | uses | fix |")
    print("|---|---|---|---|")
    for name, (rx, allow, bases, hint) in ratchet.LINTS.items():
        if name.startswith("ctl_"): continue
        cur = ratchet.lint_counts(rx, allow, bases)
        print(f"| `{name}` | {len(cur)} | {sum(cur.values())} | {hint} |")
    print()
    print("| duplicate helper | definitions |")
    print("|---|---|")
    for h, n in sorted(ratchet.helper_counts().items(), key=lambda kv: -kv[1]):
        print(f"| `{h}()` | {n} |")

if __name__ == "__main__":
    main()
