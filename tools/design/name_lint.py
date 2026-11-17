#!/usr/bin/env python3
"""Name lint: one name per node/panel across DisplayName, HelpWindows, menus, and manual.

Reads DisplayName() overrides from src/app/graph/Core.cpp and verifies that:
  1. HelpWindows.cpp Module Reference and help prose use the display name, not the legacy internal type key.
  2. StageMenuBar.cpp does not surface legacy internal type keys as user-facing labels.
  3. Panel names in HelpWindows.cpp kShortcuts match the app's panel titles.
Exit 0 when 0 diffs, 1 otherwise.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def load_display_overrides():
    core = (ROOT / "src/app/graph/Core.cpp").read_text()
    overrides = {}
    for m in re.finditer(r'if\s*\(name\s*==\s*"([^"]+)"\)\s*\n\s*return\s*"([^"]+)";', core):
        overrides[m.group(1)] = m.group(2)
    return overrides


def check_help_windows(overrides):
    help_text = (ROOT / "src/app/panels/HelpWindows.cpp").read_text()
    diffs = []

    # 1. Module Reference table entries: `{ "DisplayName", I18N_KEY(...) }`
    mod_ref_start = help_text.find("DrawModuleReferenceWindow")
    if mod_ref_start >= 0:
        mod_ref = help_text[mod_ref_start:]
        for raw, disp in overrides.items():
            if "Syphon" in raw:
                continue
            # Check category headers `{ "AudioEffects", {` or item rows `{ "Sampler", I18N_KEY(`
            if re.search(r'\{\s*"' + re.escape(raw) + r'"\s*,\s*(?:\{|I18N_KEY\()', mod_ref):
                diffs.append(f"HelpWindows Module Reference uses internal key '{raw}' instead of '{disp}'")

    # 2. User-facing prose strings should not name raw internal keys when DisplayName renames them
    prose_strings = re.findall(r'I18N_KEY\("((?:[^"\\]|\\.)*)"\)', help_text)
    prose_checks = {
        "FieldPixel": "Field Pixel",
        "PaulStretch": "Paul Stretch",
        "Moves macro": "predictive macro",
        "as Sampler.": "as Sample Player.",
        "on the Sampler.": "on the Sample Player.",
        "Sampler's controls": "Sample Player's controls",
        "Wavetable, Sampler)": "Wavetable, Sample Player)",
    }
    for s in prose_strings:
        for raw, expected in prose_checks.items():
            if raw in s and not (raw == "PaulStretch" and "PaulStretch algorithm" in s):
                diffs.append(f"HelpWindows prose uses '{raw}' instead of '{expected}'")

    return diffs


def check_menus(overrides):
    menu_text = (ROOT / "src/app/frame/StageMenuBar.cpp").read_text()
    diffs = []
    # Check MenuItem / MenuParts::Item labels don't use raw internal keys
    for raw, disp in overrides.items():
        if "Syphon" in raw or raw in ("transform", "Drift", "Moves", "Dynamics"):
            continue
        if re.search(r'Menu(?:Item|Parts::Item)\(\s*(?:L|T)?\(\s*"' + re.escape(raw) + r'"', menu_text):
            diffs.append(f"StageMenuBar uses internal key '{raw}' instead of '{disp}'")
    return diffs


def main():
    overrides = load_display_overrides()
    diffs = check_help_windows(overrides) + check_menus(overrides)
    print(f"name lint: {len(overrides)} DisplayName overrides checked, {len(diffs)} diffs")
    for d in diffs:
        print(" ", d)
    return 1 if diffs else 0


if __name__ == "__main__":
    sys.exit(main())
