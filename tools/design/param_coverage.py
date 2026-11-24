#!/usr/bin/env python3
"""How many node-body controls are drawn from params::Row descriptors (ui-formalisation Block C, section 7 target:
>= 80%, the rest named Custom with a reason).

  python3 tools/design/param_coverage.py            # totals + per file
  python3 tools/design/param_coverage.py --raw      # every control call still written by hand, file:line

A descriptor row is any `<Alias>::Slider|SliderInt|Toggle|Knob|KnobInt|Choice|ChoiceF|Colour(` in src/app/bodies -
in a table or inline in a Ui::DrawParam call. A hand call is a direct call of a control widget (ModSlider,
ModSliderInt, ModCheckbox, ModKnob, ColorSwatch, DropdownButton, AudioToggleButton, or an AudioKnobRow's
.Knob/.KnobInt/.Dropdown/.Checkbox). Custom rows are counted on their own: they are descriptors whose drawing is
by hand.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
BODIES = ROOT / "src/app/bodies"
ROW = re.compile(r'\b\w+Row::(Slider|SliderInt|Toggle|Knob|KnobInt|Choice|ChoiceF|Colour)\s*(?:<[^>]*>)?\(')
CUSTOM = re.compile(r'\b\w+Row::Custom\(')
RAW = re.compile(r'(?<![\w:.>])(ModSlider|ModSliderInt|ModCheckbox|ModKnob|ColorSwatch|DropdownButton|AudioToggleButton)\s*\('
                 r'|\b\w+\.(Knob|KnobInt|Dropdown|Checkbox)\s*\(')


def strip_comments(line):
    i = line.find("//")
    return line if i < 0 else line[:i]


def main():
    show_raw = "--raw" in sys.argv
    tot_rows = tot_custom = tot_raw = 0
    per = []
    for p in sorted(BODIES.glob("*.cpp")):
        rows = custom = raw = 0
        for no, line in enumerate(p.read_text().split("\n"), 1):
            code = strip_comments(line)
            rows += len(ROW.findall(code))
            custom += len(CUSTOM.findall(code))
            for m in RAW.finditer(code):
                raw += 1
                if show_raw:
                    print(f"{p.relative_to(ROOT)}:{no}: {code.strip()[:140]}")
        if rows or raw or custom:
            per.append((p.name, rows, custom, raw))
        tot_rows, tot_custom, tot_raw = tot_rows + rows, tot_custom + custom, tot_raw + raw
    if not show_raw:
        print(f"{'file':28} {'rows':>6} {'custom':>6} {'hand':>6} {'cover':>6}")
        for name, r, c, h in per:
            print(f"{name:28} {r:6} {c:6} {h:6} {100.0 * (r + c) / max(1, r + c + h):5.1f}%")
    total = tot_rows + tot_custom + tot_raw
    print(f"descriptor rows {tot_rows}, custom rows {tot_custom}, hand calls {tot_raw}: "
          f"{100.0 * (tot_rows + tot_custom) / max(1, total):.1f}% drawn from descriptors")


if __name__ == "__main__":
    main()
