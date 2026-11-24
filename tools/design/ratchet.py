#!/usr/bin/env python3
"""Design ratchet: per-file counts of literal colours, raw widget calls and size literals may only go down.

  ratchet.py            check against ratchet.json (exit 1 if any file went up or a new file has any)
  ratchet.py --update   lower the stored counts to current (never raises them)
  ratchet.py            also prints the 10 worst files per count, so each pass picks those first
  ratchet.py --reset    store the current counts as the baseline (once, when a UI system sheet step lands)
Also verifies Tokens.gen.h matches tokens.json.

LINTS below are the UI lints from internal/plans/ui-hardcode-review.md table 7d: each is a per-file count that may
only go down. HELPERS lists kit helpers that must have one definition; their total count may only go down too.
A lint missing from ratchet.json is seeded from the current tree on the next --update.
"""
import json, pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
STORE = ROOT / "tools/design/ratchet.json"
RX = re.compile(r"IM_COL32\(\s*\d+\s*,\s*\d+\s*,\s*\d+\s*,\s*\d+\s*\)|ImVec4\(\s*[0-9.]+f\s*,\s*[0-9.]+f\s*,\s*[0-9.]+f\s*,\s*[0-9.]+f\s*\)")
# Plan section 5: a surface never calls these; it uses a component. Existing calls are the baseline, new ones fail.
RAW = re.compile(r"ImGui::(Button|SmallButton|Separator|SameLine|PushStyleColor|PushStyleVar)\(")
# UI system sheet section 5: a height, icon size or radius is a token, never a number. Counts named size
# constants set to a literal and rect corners given as a literal. Components count too.
SIZE = re.compile(r"(?:constexpr|const)\s+float\s+k\w*(?:H|Height|Row\w*|Radius|Rounding|Icon\w*|Chip\w*|Btn\w*)\s*=\s*[0-9][0-9.]*f?\s*;"
                  r"|AddRect(?:Filled)?\([^;]*?,\s*[0-9][0-9.]*f\s*\)\s*;")
FONT_SCALE = re.compile(r"ImGui::SetWindowFontScale\(|ImGui::GetFontSize\(\)\s*\*\s*0\.(?!5f|74f)[0-9]+f?")
ROUNDING = re.compile(
    r"ImGui::PushStyleVar\(ImGuiStyleVar_(?:Frame|Window|Child|Popup|Tab|Grab)Rounding,\s*[^;]*?[1-9][0-9.]*f"
    r"|style\.(?:Frame|Window|Child|Popup|Tab|Grab)Rounding\s*=\s*[1-9][0-9.]*f"
    r"|ed::PushStyleVar\(ed::StyleVar_NodeRounding,\s*[0-9.]+f"
    r"|float\s+rounding\s*=\s*[0-9.]+f"
)
POPUP = re.compile(r"ImGui::BeginPopup\(")
ALLOW = {"src/app/ui/design/", "src/app/selftest/UiAudit.cpp"}  # components themselves; UiAudit draws raw widgets on purpose as canaries

KIT = {"src/app/ui/design/"}
SELFTEST = {"src/app/selftest/"}
CHROME = ("src/app/frame", "src/app/panels/TerminalPanel.cpp", "src/app/panels/ModMatrixPanel.cpp", "src/app/panels/ChatPanel.cpp",
          "src/app/panels/HelpWindows.cpp", "src/app/panels/SettingsWindow.cpp", "src/app/panels/LibraryPanels.cpp")
# name: (regex, allowed path prefixes, bases, fix hint)
LINTS = {
    "alphas": (re.compile(r"ImVec4\(\s*[\w.]+f?\s*,\s*[\w.]+f?\s*,\s*[\w.]+f?\s*,\s*0?\.[0-9]+f?\s*\)"
                          r"|GetColorU32\(ImGuiCol_\w+\s*,\s*0?\.[0-9]+f?\s*\)|\.w\s*\*?=\s*0?\.[0-9]+f\s*;"),
               SELFTEST, ("src/app", "src/arrange", "src/nodes"), "use a tok::alpha_* token"),
    "palette": (re.compile(r"tok::palf?::"), SELFTEST, ("src/app", "src/arrange", "src/nodes"),
                "use a role token, not a colour named by its value"),
    "font_sizes": (re.compile(r"GetFontSize\(\)\s*\*\s*(?!0\.5f?\b)[0-9][0-9.]*f?"), SELFTEST, ("src/app",),
                   "use UiType::Px or a tok:: size (tip_wrap_em, page_swatch)"),
    "drag_thresholds": (re.compile(r"IsMouseDragging\([^,()]+,\s*[1-9][0-9.]*f?\s*\)"), SELFTEST, ("src/app",),
                        "use the shared drag threshold"),
    "tooltips": (re.compile(r"ImGui::(?:SetTooltip|SetItemTooltip|BeginTooltip|BeginItemTooltip)\("), KIT | SELFTEST, ("src/app",),
                 "use the shared tooltip component"),
    "raw_inputs": (re.compile(r"ImGui::(?:CollapsingHeader|TreeNode\w*|InputText\w*|DragFloat\d?|DragInt\d?|SliderFloat\d?|SliderInt\d?"
                              r"|ColorButton|ColorEdit\d|ColorPicker\d)\("), KIT | SELFTEST, ("src/app",),
                   "use FieldWell / FormParts / PanelKit::FoldHeader / ColourChip"),
    "id_masks": (re.compile(r"GetID\([^()]*\)\s*\)?\s*[&%]"), SELFTEST, ("src/app",), "pass the whole ImGuiID; a masked id collides"),
    "line_icons": (re.compile(r"AddLine\(|AddTriangleFilled\("), SELFTEST, CHROME, "draw an IconsInfinite glyph with glyph::Draw"),
    "layout_literals": (re.compile(r"SameLine\(\s*[^,()]*,\s*[1-9][0-9.]*f?\s*\)|Dummy\(ImVec2\(\s*[1-9][0-9.]*f?\s*,"
                                   r"|SetNextWindowSize\(ImVec2\(\s*[1-9]|PushStyleVar\(ImGuiStyleVar_\w+,\s*ImVec2\(\s*[1-9][0-9.]*f?"),
                        SELFTEST, ("src/app",), "use tok::space_* / window tokens"),
}
LINTS.update({
    # Whole-number sizes (ImVec2(620, 460), SameLine(8), Dummy(ImVec2(120, 0))): the decimal size rule above misses them.
    "int_sizes": (re.compile(r"(?:ImVec2|SameLine|Dummy|Indent|SetNextItemWidth|PushItemWidth|SetColumnWidth|PushTextWrapPos|UiLayout::Fixed)"
                             r"\((?:[^;()]|\([^;()]*\))*?(?<![\w.#x])(?:[3-9]|[1-9][0-9]+)(?![\w.])"),
                  SELFTEST, ("src/app", "src/core/LauncherCard.h", "src/core/SplashScreen.h"), "use a tok:: size"),
    "timings": (re.compile(r"(?:GetTime\(\)|DeltaTime)[^;]*?(?<![\w.])(?:[0-9]+\.[0-9]+|[1-9][0-9]+(?:\.0)?)f?(?![\w.])"
                           r"|(?<![\w.])(?:[0-9]+\.[0-9]+|[1-9][0-9]+(?:\.0)?)f?(?![\w.])[^;]*?(?:GetTime\(\)|DeltaTime)"),
                KIT | SELFTEST, ("src/app",), "use tok::motion_* (motion_ms eases, motion_s holds, motion_rate, motion_hz)"),
    "raw_labels": (re.compile(r"ImGui::(?:Text|TextDisabled|TextWrapped|BulletText|TextUnformatted|MenuItem|BeginMenu|Selectable|Button|SmallButton"
                              r"|Checkbox|SeparatorText)\(\s*\"(?!##)(?:%[-+ #0-9.*]*[a-zA-Z]|[^\"%A-Za-z])*[A-Za-z][^\"]*\"\s*[,)]"),
                   SELFTEST | {"src/app/bodies/", "src/app/ui/ParamWidgets.cpp"}, ("src/app",),
                   "wrap visible text in T() and widget labels in L() (tools/i18n/lint.py is the exact check; bodies stay English, decision D2)"),
    "key_checks": (re.compile(r"IsKeyPressed\(\s*ImGuiKey_|IsKeyChordPressed\(|ImGuiMod_(?:Ctrl|Shift|Alt|Super)"), KIT | SELFTEST, ("src/app",),
                   "read the chord from the keymap (chunk 3)"),
})
# One count per control type (internal/plans/ui-control-catalogue.md). name: (regex, kit part or "none yet")
CONTROLS = {
    "buttons": (r"ImGui::(?:Button|SmallButton|ArrowButton|ImageButton)\(", "ActionButton / TextButton / IconTile"),
    "hit_areas": (r"ImGui::InvisibleButton\(", "none yet (UiInteract hit area)"),
    "checkboxes": (r"ImGui::(?:Checkbox|RadioButton)\(", "CheckBox / Switch"),
    "sliders": (r"ImGui::(?:SliderFloat|SliderInt|VSliderFloat|DragFloat|DragInt)\w*\(", "FieldWell (numeric) / VFader"),
    "text_inputs": (r"ImGui::InputText\w*\(|ImGui::InputFloat\w*\(|ImGui::InputInt\w*\(", "FieldWell / FilterBar"),
    "dropdowns": (r"ImGui::(?:BeginCombo|Combo)\(", "DropdownField"),
    "colour_pickers": (r"ImGui::(?:ColorButton|ColorEdit\d|ColorPicker\d)\(", "ColourChip"),
    "popups": (r"ImGui::(?:BeginPopup|BeginPopupModal)\(", "MenuParts / DialogParts"),
    "context_menus": (r"ImGui::BeginPopupContext\w*\(|IsMouseClicked\(\s*(?:ImGuiMouseButton_Right|1)\s*\)|IsItemClicked\(\s*(?:ImGuiMouseButton_Right|1)\s*\)",
                      "none yet (MenuParts::Context)"),
    "menus": (r"ImGui::(?:BeginMenu|MenuItem)\(", "MenuParts"),
    "tables_lists": (r"ImGui::(?:BeginTable|Selectable|BeginListBox)\(", "none yet (ListRow)"),
    "tree_folds": (r"ImGui::(?:CollapsingHeader|TreeNode\w*)\(", "PanelKit::FoldHeader"),
    "tabs": (r"ImGui::(?:BeginTabBar|BeginTabItem|TabItemButton)\(", "PillGroup / PagePicker"),
    "progress": (r"ImGui::ProgressBar\(", "BusyLine"),
    "separators": (r"ImGui::(?:Separator|SeparatorText)\(", "Divider"),
    "child_windows": (r"ImGui::BeginChild\(", "PanelFrame / SectionCard"),
    "cursors": (r"SetMouseCursor\(", "ToolCursor"),
    "focus": (r"SetKeyboardFocusHere\(|SetItemDefaultFocus\(|IsWindowFocused\(", "none yet (UiInteract focus ring)"),
    "scrollbar_style": (r"ImGuiStyleVar_Scrollbar\w*|ImGuiCol_Scrollbar\w*", "Theme (one scrollbar style)"),
    "style_colours": (r"PushStyleColor\(", "component states (hover/press/on/disabled)"),
    "style_vars": (r"PushStyleVar\(", "UiDensity / components"),
    "fonts": (r"PushFont\(|SetWindowFontScale\(", "UiType"),
    "images": (r"ImGui::Image\(|AddImage\w*\(", "none yet (Thumb)"),
    "drag_drop": (r"BeginDragDropSource\(|BeginDragDropTarget\(", "ReorderDrag"),
    "disabled": (r"ImGui::BeginDisabled\(", "component disabled state"),
    "hand_text": (r"->AddText\(", "UiType / Readout"),
    "hand_shapes": (r"->Add(?:Rect|RectFilled|Circle|CircleFilled|Line|Triangle|TriangleFilled|Quad|QuadFilled|Bezier\w*|Polyline|ConvexPolyFilled)\(",
                    "components (no hand-drawn chrome)"),
    "canvas_pins": (r"ed::BeginPin\(|ed::PinRect\(", "PinDot"),
    # A knob drawn outside the kit: ImGui knob add-ons or a hand-rolled Draw*Knob. Row calls (r.Knob, KnobRow) are the kit's
    # layout and end in ModKnob (or DeskParts::Pot, the desk skin), so they do not count (owner 2026-11-24: one Knob).
    "knobs": (r"ImGuiKnobs::|(?<![\w.>:])Draw\w+Knob\(|ImGui::Knob\w*\(", "ModWidgets knob (one Knob)"),
    "wires": (r"ed::Link\(", "none yet (Wire: one cable look)"),
    "meters": (r"\b\w*Meter\(", "AudioViz meter"),
    "faders": (r"\b(?:Draw)?\w*Fader\((?!.*VFader)", "VFader"),
}
for _n, (_rx, _part) in CONTROLS.items():
    LINTS["ctl_" + _n] = (re.compile(_rx), KIT | SELFTEST, ("src/app",), f"use {_part}")

# Kit helpers that should exist once (chunks 2-3 of the review collapse them; the count may only go down).
HELPERS = ("WithAlpha", "TextTint", "Tint", "TruncateWithEllipsis", "IconChip", "TextChip", "TextW", "PickerFace", "Picker",
           "PushToolbarChip", "UndoOnToggle", "DrawConsoleMeter", "ConsoleMeter", "DrawStripMeter", "IsBlackKey", "ColourChips",
           "SectionHeader", "EdgeCol", "WellCombo")

def lint_counts(rx, allow, bases):
    out = {}
    for base in bases:
        b = ROOT / base
        files = [b] if b.is_file() else sorted(b.rglob("*"))
        for f in files:
            if f.suffix not in (".cpp", ".h", ".mm"): continue
            rel = str(f.relative_to(ROOT))
            if any(rel.startswith(a) for a in allow): continue
            text = f.read_text(errors="ignore")
            if rx is LINTS["raw_labels"][0]:
                text = english_regions_out(text)
            n = len(rx.findall(text))
            if n: out[rel] = n
    return out

def english_regions_out(text):
    """raw_labels skips what tools/i18n/lint.py skips: // i18n-skip-begin .. end regions and lines tagged i18n-ok
    (English by owner decision D2, e.g. the Field language reference)."""
    text = re.sub(r"i18n-skip-begin.*?i18n-skip-end", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return "\n".join("" if "i18n-ok" in l else l for l in text.split("\n"))

def helper_counts():
    rx = {h: re.compile(r"^[ \t]*(?:static\s+|inline\s+|constexpr\s+)*[\w:<>*&]+[ \t]+[*&]?" + h + r"\s*\([^;]*$", re.M) for h in HELPERS}
    out = {h: 0 for h in HELPERS}
    for f in sorted((ROOT / "src/app").rglob("*")):
        if f.suffix not in (".cpp", ".h", ".mm") or "selftest" in f.parts: continue
        t = f.read_text(errors="ignore")
        for h, r in rx.items():
            out[h] += len(r.findall(t))
    return {h: n for h, n in out.items() if n}

def counts(rx=RX, allow=None, bases=("src/app", "src/arrange", "src/nodes")):
    allow = ALLOW if allow is None else allow
    out = {}
    for base in bases:
        for f in sorted((ROOT / base).rglob("*")):
            if f.suffix not in (".cpp", ".h", ".mm"): continue
            rel = str(f.relative_to(ROOT))
            if any(rel.startswith(a) for a in allow): continue
            n = len(rx.findall(f.read_text(errors="ignore")))
            if n: out[rel] = n
    return out

def load():
    raw = json.loads(STORE.read_text()) if STORE.exists() else {}
    if "colours" not in raw and "widgets" not in raw:   # old flat format
        raw = {"colours": raw, "widgets": {}}
    return raw.get("colours", {}), raw.get("widgets", {}), raw.get("sizes", {}), raw.get("font_scales", {}), raw.get("roundings", {}), raw.get("popups", {})

def load_lints():
    raw = json.loads(STORE.read_text()) if STORE.exists() else {}
    return raw.get("lints", {}), raw.get("helpers", None)

def store(c, w, z, f, rn, pp, lints=None, helpers=None):
    if lints is None or helpers is None:
        old_l, old_h = load_lints()
        lints = old_l if lints is None else lints
        helpers = old_h if helpers is None else helpers
    keep = json.loads(STORE.read_text()) if STORE.exists() else {}   # keys other tools own (ui_audit's "audit") survive
    keep.update({"colours": dict(sorted(c.items())), "widgets": dict(sorted(w.items())),
                                 "sizes": dict(sorted(z.items())), "font_scales": dict(sorted(f.items())),
                                 "roundings": dict(sorted(rn.items())), "popups": dict(sorted(pp.items())),
                                 "lints": {k: dict(sorted(v.items())) for k, v in sorted(lints.items())},
                                 "helpers": dict(sorted((helpers or {}).items()))})
    STORE.write_text(json.dumps(keep, indent=1) + "\n")
    print(f"ratchet: {sum(c.values())} literal colours, {sum(w.values())} raw widget calls, {sum(z.values())} size literals, {sum(f.values())} font scales, {sum(rn.values())} rounding literals, {sum(pp.values())} raw popups")

def main():
    cur_c, cur_w, cur_z, cur_f, cur_r, cur_p = (
        counts(RX, bases=("src/app", "src/arrange", "src/nodes")),
        counts(RAW),
        counts(SIZE, set()),
        counts(FONT_SCALE),
        counts(ROUNDING, set()),
        counts(POPUP),
    )
    old_c, old_w, old_z, old_f, old_r, old_p = load()
    cur_l = {name: lint_counts(rx, allow, bases) for name, (rx, allow, bases, _) in LINTS.items()}
    cur_h = helper_counts()
    old_l, old_h = load_lints()
    audit = [sys.executable, str(ROOT / "tools/design/ui_audit.py"), "--ratchet"]
    if "--reset" in sys.argv:
        store(cur_c, cur_w, cur_z, cur_f, cur_r, cur_p, cur_l, cur_h); return subprocess.run(audit + ["--reset"]).returncode
    if "--update" in sys.argv:
        low = lambda cur, old: {k: min(v, old.get(k, v)) for k, v in cur.items()}
        new_l = {n: (low(cur_l[n], old_l[n]) if n in old_l else cur_l[n]) for n in LINTS}
        new_h = cur_h if old_h is None else {h: min(v, old_h.get(h, v)) for h, v in cur_h.items()}
        store(low(cur_c, old_c), low(cur_w, old_w), low(cur_z, old_z), low(cur_f, old_f), low(cur_r, old_r), low(cur_p, old_p), new_l, new_h)
        return subprocess.run(audit + ["--update"]).returncode
    badl = []
    for name, (_, _, _, hint) in LINTS.items():
        if name not in old_l:
            print(f"ratchet: lint '{name}' has no baseline yet: run ratchet.py --update to seed it"); continue
        for k, v in cur_l[name].items():
            if v > old_l[name].get(k, 0):
                badl.append(k); print(f"RATCHET: {k} has {v} {name} (allowed {old_l[name].get(k, 0)}): {hint}")
    if old_h is not None:
        for h, v in cur_h.items():
            if v > old_h.get(h, 0):
                badl.append(h); print(f"RATCHET: {v} definitions of {h}() (allowed {old_h.get(h, 0)}): call the design-kit one")
    bad = [(k, v, old_c.get(k, 0)) for k, v in cur_c.items() if v > old_c.get(k, 0)]
    for k, v, o in bad: print(f"RATCHET: {k} has {v} literal colours (allowed {o}): use a role token")
    badw = [(k, v, old_w.get(k, 0)) for k, v in cur_w.items() if v > old_w.get(k, 0)]
    for k, v, o in badw: print(f"RATCHET: {k} has {v} raw widget calls (allowed {o}): use a component from ui/design/components")
    badz = [(k, v, old_z.get(k, 0)) for k, v in cur_z.items() if v > old_z.get(k, 0)]
    for k, v, o in badz: print(f"RATCHET: {k} has {v} size literals (allowed {o}): use control_h / row_h / radius_* / icon_control")
    badf = [(k, v, old_f.get(k, 0)) for k, v in cur_f.items() if v > old_f.get(k, 0)]
    for k, v, o in badf: print(f"RATCHET: {k} has {v} font scales (allowed {o}): use UiType::Push / UiType::Px")
    badr = [(k, v, old_r.get(k, 0)) for k, v in cur_r.items() if v > old_r.get(k, 0)]
    for k, v, o in badr: print(f"RATCHET: {k} has {v} rounding literals (allowed {o}): use tok::radius_control / tok::radius_container")
    badp = [(k, v, old_p.get(k, 0)) for k, v in cur_p.items() if v > old_p.get(k, 0)]
    for k, v, o in badp: print(f"RATCHET: {k} has {v} raw popups (allowed {o}): use MenuParts::BeginPopup")
    print("lints: " + ", ".join(f"{n} {sum(v.values())} (stored {sum(old_l.get(n, {}).values())})" for n, v in cur_l.items())
          + f"; helper definitions {sum(cur_h.values())} (stored {sum((old_h or {}).values())})")
    for name, cur in (("literal colours", cur_c), ("raw widget calls", cur_w), ("size literals", cur_z), ("font scales", cur_f), ("rounding literals", cur_r), ("raw popups", cur_p)):
        top = sorted(cur.items(), key=lambda kv: -kv[1])[:10]
        print(f"top {name}: " + ", ".join(f"{pathlib.Path(k).name} {v}" for k, v in top))
    r = subprocess.run([sys.executable, str(ROOT / "tools/design/build_tokens.py"), "--check"])
    print(f"ratchet: colours {sum(cur_c.values())} (stored {sum(old_c.values())}), raw widgets {sum(cur_w.values())} (stored {sum(old_w.values())}), "
          f"sizes {sum(cur_z.values())} (stored {sum(old_z.values())}), font scales {sum(cur_f.values())} (stored {sum(old_f.values())}), "
          f"roundings {sum(cur_r.values())} (stored {sum(old_r.values())}), popups {sum(cur_p.values())} (stored {sum(old_p.values())})")
    # A bare EndChild aborts when the child's last act was a SetCursorPos (crash 2026-11-17): always 0, no baseline.
    rawend = []
    for f in sorted((ROOT / "src/app").rglob("*.[ch]*")):
        for i, line in enumerate(f.read_text(errors="ignore").splitlines(), 1):
            if "ImGui::EndChild()" in line and "inline void EndChildSafe" not in line and f.name != "AppCommon.h":
                rawend.append(f"{f.relative_to(ROOT)}:{i}")
    for x in rawend: print(f"RATCHET: {x} calls ImGui::EndChild(): use EndChildSafe()")
    print(f"ratchet: raw EndChild {len(rawend)} (allowed 0)")
    # Token names say the job (owner 2026-11-24): no _arg_N / _draw_x / _ratio_N / trailing-number names: always 0.
    rn = subprocess.run([sys.executable, str(ROOT / "tools/design/token_names.py"), "--lint"])
    ra = subprocess.run(audit)   # per-category counts from the tokenizer scan (tools/design/ui_audit.py)
    return 1 if bad or badw or badz or badf or badr or badp or badl or rawend or r.returncode or ra.returncode or rn.returncode else 0

if __name__ == "__main__":
    sys.exit(main())
