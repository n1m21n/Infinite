#!/usr/bin/env python3
"""Keys gate: single static gate for keyboard shortcuts across all areas and OSes.

Combines overlap/OS checks (formerly keys_lint.py) and table-vs-code drift
checks (formerly shortcuts-sweep/check.py) into one script:
  1. Overlap: inside one area (category), a chord does one thing (excluding MODE_ACTIONS).
  2. App-wide vs panel yield: an app-wide chord is not also a panel's chord unless listed in YIELDS.
  3. 3 OSes: checks macOS (MODKEY=Cmd, TERMKEY=Cmd), Windows/Linux (MODKEY=Ctrl, TERMKEY=Ctrl+Shift).
  4. OS-reserved keys: no row takes a key the OS keeps for itself (unless in OS_OWN).
  5. Table vs code drift:
     - Documented in kShortcuts but no IsKeyPressed/IsKeyDown handler in src/.
     - Handled in src/ but absent from kShortcuts (and not in LOCAL_KEYS).
  6. macOS-only modifier: KeySuper/ImGuiMod_Super without KeyCtrl/ImGuiMod_Ctrl (unless in SUPER_ALLOW).

Exit 0 when clean, 1 on any problem. Pass --quiet for a single-line summary.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC_HELP = ROOT / "src/app/panels/HelpWindows.cpp"

sys.path.insert(0, str(ROOT / "scripts"))
import appsrc

# ---------------------------------------------------------------------------
# 1. Overlap & OS rules (from keys_lint.py)
# ---------------------------------------------------------------------------

GLOBAL_CATS = {"File & Patch"}
GLOBAL_ACTIONS = {"UI Scale", "Play / Pause", "Show Lock", "Panic", "Next / Previous Show Patch"}

# (chord, area) -> (file, text that makes the app-wide handler step aside for that area)
YIELDS = {
    ("Cmd+0", "2D View"): ("src/app/frame/StagePresent.cpp", "viewHasKeys"),
    ("Cmd+=", "2D View"): ("src/app/frame/StagePresent.cpp", "viewHasKeys"),
    ("Cmd+-", "2D View"): ("src/app/frame/StagePresent.cpp", "viewHasKeys"),
    ("Cmd+0", "3D View"): ("src/app/frame/StagePresent.cpp", "viewHasKeys"),
    ("Cmd+=", "3D View"): ("src/app/frame/StagePresent.cpp", "viewHasKeys"),
    ("Cmd+-", "3D View"): ("src/app/frame/StagePresent.cpp", "viewHasKeys"),
}

ROW_RX = re.compile(
    r'\{\s*I18N_KEY\("([^"]*)"\),\s*I18N_KEY\("([^"]*)"\),\s*((?:MODKEY|TERMKEY|"(?:[^"\\]|\\.)*"|\s)*),\s*I18N_KEY\('
)
MODS = ("Cmd", "Ctrl", "Shift", "Alt", "Option")
NAV = {"Up", "Down", "Left", "Right", "PageUp", "PageDown", "Home", "End"}
MODE_ACTIONS = {"Fly", "Turn / Clear Bone", "Edit Operations", "Pose Mode", "Sculpt Mode", "Nudge Param", "Close Picture"}

PLATFORMS = {
    "macOS": {"MODKEY": "Cmd", "TERMKEY": "Cmd"},
    "Windows": {"MODKEY": "Ctrl", "TERMKEY": "Ctrl+Shift"},
    "Linux": {"MODKEY": "Ctrl", "TERMKEY": "Ctrl+Shift"},
}

RESERVED = {
    "macOS": {
        "Cmd+Tab", "Cmd+Space", "Cmd+H", "Cmd+M", "Cmd+Q", "Cmd+`", "Cmd+Alt+Esc",
        "Cmd+Shift+3", "Cmd+Shift+4", "Cmd+Shift+5", "Ctrl+Up", "Ctrl+Down",
        "Ctrl+Left", "Ctrl+Right", "Ctrl+Cmd+Q", "Cmd+Alt+H", "Cmd+Alt+D",
    },
    "Windows": {
        "Alt+F4", "Alt+Tab", "Alt+Space", "Alt+Esc", "Ctrl+Esc", "Ctrl+Alt+Delete",
        "Ctrl+Shift+Esc", "Ctrl+Alt+Del", "Win+D", "Win+L", "Win+E", "Win+Tab",
    },
    "Linux": {
        "Alt+F4", "Alt+Tab", "Alt+F2", "Alt+Space", "Alt+F7", "Alt+F8",
        "Ctrl+Alt+T", "Ctrl+Alt+Delete", "Ctrl+Alt+Del", "Ctrl+Alt+Left",
        "Ctrl+Alt+Right", "Ctrl+Alt+Up", "Ctrl+Alt+Down",
    },
}
OS_OWN = {"Quit", "Hide", "Minimize", "Minimise"}

# Core verbs: one canonical chord app-wide (written with macOS's Cmd for MODIS/MODKEY; Ctrl on Win/Linux).
CORE_VERBS = {
    "Cut": "Cmd+X",
    "Copy": "Cmd+C",
    "Paste": "Cmd+V",
    "Duplicate": "Cmd+D",
    "Delete": "Delete",
    "Select All": "Cmd+A",
    "Group": "Cmd+G",
    "Ungroup": "Cmd+Shift+G",
    "Undo": "Cmd+Z",
    "Redo": "Cmd+Shift+Z",
    "Zoom": "=",
    "Frame": "F",
}

# Intentional domain-specific exceptions where a verb cannot use the canonical chord:
#   Terminal: Ctrl+C/V are shell signals/literals on Windows/Linux, so TERMKEY (Ctrl+Shift) is used.
#   2D View: Cmd+D is Deselect (Photoshop convention), so Duplicate Layer uses Cmd+J; Zoom uses Cmd+= / Cmd+-.
#   3D View: A/Alt+A and Shift+D follow Blender convention (Cmd+D is Duplicate Scene).
CORE_VERB_ALLOW = {
    ("Terminal", "Copy"),
    ("Terminal", "Paste"),
    ("2D View", "Duplicate"),
    ("2D View", "Zoom"),
    ("3D View", "Duplicate"),
    ("3D View", "Select All"),
}

# Exact action-name -> list of (verb, chord_index_or_None) for kShortcuts rows that express core verbs.
# When chord_index is None, all chords in the row belong to that verb; when an int, that slash-separated piece.
CORE_ACTION_MAP = {
    "Undo": [("Undo", None)],
    "Redo": [("Redo", None)],
    "Undo / Redo Studio Edit": [("Undo", 0), ("Redo", 1)],
    "Copy": [("Copy", None)],
    "Paste": [("Paste", None)],
    "Copy / Paste": [("Copy", 0), ("Paste", 1)],
    "Copy / Paste Clips": [("Copy", 0), ("Paste", 1)],
    "Copy / Cut / Paste Controls": [("Copy", 0), ("Cut", 1), ("Paste", 2)],
    "Copy / Cut / Paste Slices": [("Copy", 0), ("Cut", 1), ("Paste", 2)],
    "Copy / Cut / Paste Notes": [("Copy", 0), ("Cut", 1), ("Paste", 2)],
    "Copy / Cut / Paste Boxes": [("Copy", 0), ("Cut", 1), ("Paste", 2)],
    "Copy / Cut / Paste Clip": [("Copy", 0), ("Cut", 1), ("Paste", 2)],
    "Copy / Cut / Paste": [("Copy", 0), ("Cut", 1), ("Paste", 2)],
    "Copy / Clear Selection": [("Copy", 0)],
    "Copy / Paste / Duplicate Scene": [("Copy", 0), ("Paste", 1), ("Duplicate", 2)],
    "Duplicate": [("Duplicate", None)],
    "Duplicate Controls": [("Duplicate", None)],
    "Duplicate Notes": [("Duplicate", None)],
    "Duplicate Boxes": [("Duplicate", None)],
    "Duplicate Clips": [("Duplicate", None)],
    "Duplicate Clip": [("Duplicate", None)],
    "Duplicate Layer": [("Duplicate", None)],
    "Delete": [("Delete", None)],
    "Delete Controls": [("Delete", None)],
    "Delete Slices": [("Delete", None)],
    "Delete Notes": [("Delete", None)],
    "Delete Box or Wire": [("Delete", None)],
    "Delete Points": [("Delete", None)],
    "Delete Clips": [("Delete", None)],
    "Clear Clip": [("Delete", None)],
    "Delete Layer": [("Delete", None)],
    "Remove": [("Delete", None)],
    "Select All": [("Select All", None)],
    "Select All Controls": [("Select All", None)],
    "Select All Slices": [("Select All", None)],
    "Select All Notes": [("Select All", None)],
    "Select All Boxes": [("Select All", None)],
    "Select All Points": [("Select All", None)],
    "Select All Clips": [("Select All", None)],
    "Select All / None": [("Select All", None)],
    "Group Selection": [("Group", None)],
    "Ungroup": [("Ungroup", None)],
    "Group / Ungroup Clips": [("Group", 0), ("Ungroup", 1)],
    "Group / Ungroup": [("Group", 0), ("Ungroup", 1)],
    "Zoom Sample Studio": [("Zoom", None)],
    "Zoom Piano Roll": [("Zoom", None)],
    "Zoom Timeline In / Out": [("Zoom", None)],
    "Zoom 2D View": [("Zoom", None)],
    "Zoom 3D View": [("Zoom", None)],
    "Fit View to Content": [("Frame", None)],
    "Fit Page": [("Frame", None)],
    "Fit Curve": [("Frame", None)],
    "Zoom Timeline to Fit": [("Frame", None)],
    "Frame": [("Frame", None)],
}

# ---------------------------------------------------------------------------
# 2. Table-vs-code drift rules (from shortcuts-sweep/check.py)
# ---------------------------------------------------------------------------

KEY_ALIASES = {
    "delete": "Delete", "backspace": "Backspace", "space": "Space",
    "/": "Slash", "escape": "Escape", "enter": "Enter", "tab": "Tab",
    "scroll wheel": None, "drag canvas": None, "shift + drag": None,
    "left": "LeftArrow", "right": "RightArrow", "up": "UpArrow", "down": "DownArrow",
    "home": "Home", "end": "End", "pagedown": "PageDown", "pageup": "PageUp",
    "=": "Equal", "-": "Minus", ".": "Period",
    "`": "GraveAccent", "period": "Period", "comma": "Comma", "f2": "F2", "f5": "F5", "f12": "F12",
    "[": "LeftBracket", "]": "RightBracket", ";": "Semicolon", "'": "Apostrophe",
}

LOCAL_KEYS = {
    "Escape": "dismisses a popup/modal/inline edit - not a global binding",
    "Enter": "commits an inline text edit - not a global binding",
    "KeypadAdd": "the keypad twin of = in zoom handlers",
    "KeypadSubtract": "the keypad twin of - in zoom handlers",
    "KeypadEnter": "Enter's keypad twin - opens a hovered comment's inline edit, not a global binding",
    "Minus": "zoom-out within the canvas zoom handler, paired with the scroll gesture",
    "KeypadDecimal": "the keypad twin of F (frame) in the 3D View, and the decimal point while typing an amount",
    "KeypadDivide": "the keypad twin of / (local view) in the 3D View",
}

SUPER_ALLOW = [
    "key(ImGuiMod_Super, true); key(ImGuiKey_C, true);",
    "key(ImGuiKey_V, false); key(ImGuiMod_Super, false);",
    "const bool physicalCtrl = io.KeySuper;  // ImGui swaps Cmd and Ctrl on macOS",
]


def key_text(expr, plat="macOS"):
    out = ""
    for m in re.finditer(r'(MODKEY|TERMKEY)|"((?:[^"\\]|\\.)*)"', expr):
        out += PLATFORMS[plat][m.group(1)] if m.group(1) else m.group(2)
    return out


def chords(text):
    """'Cmd+Shift+Z / Cmd+Y' -> ['Cmd+Shift+Z', 'Cmd+Y']; 'Cmd+T / W' -> ['Cmd+T', 'Cmd+W']."""
    text = re.sub(r"\(.*?\)", "", text)
    out, prefix = [], ""
    for piece in [p.strip() for p in text.split(" / ")]:
        if not piece:
            continue
        parts = piece.split("+")
        if len(parts) > 1 and all(p in MODS for p in parts[:-1]):
            prefix = "+".join(parts[:-1]) + "+"
            out.append(piece)
        elif prefix and piece in NAV:
            out.append(prefix + piece)
        else:
            prefix = ""
            out.append(piece)
    return [c for c in out if not re.search(r"Drag|Click|Scroll|Wheel|\.\.\.|Hold", c)]


def norm_key(tok):
    """One key token from the help table -> the ImGuiKey_ suffix it should map to."""
    t = tok.strip().lower()
    if t in KEY_ALIASES:
        return KEY_ALIASES[t]
    if len(t) == 1 and t.isalpha():
        return t.upper()
    if len(t) == 1 and t.isdigit():
        return t
    m = re.fullmatch(r"keypad\s*(\d)", t)
    if m:
        return "Keypad" + m.group(1)
    if len(t) == 1 and t == "/":
        return "Slash"
    return None


def check_overlap_and_os():
    text = SRC_HELP.read_text()
    bad, counts = [], {}
    for plat in PLATFORMS:
        rows = []
        for m in ROW_RX.finditer(text):
            cat, act, expr = m.group(1), m.group(2), m.group(3)
            for c in chords(key_text(expr, plat)):
                rows.append((cat, act, c))

        by_cat = {}
        for cat, act, c in rows:
            if act in MODE_ACTIONS:
                continue
            by_cat.setdefault((cat, c.lower()), []).append(act)
        for (cat, c), acts in sorted(by_cat.items()):
            if len(set(acts)) > 1:
                bad.append(f"[{plat}] {cat}: {c} does {len(set(acts))} things: {', '.join(sorted(set(acts)))}")

        glob = [(cat, act, c) for cat, act, c in rows if cat in GLOBAL_CATS or act in GLOBAL_ACTIONS]
        for gcat, gact, gc in glob:
            for cat, act, c in rows:
                if cat == gcat or c.lower() != gc.lower() or (cat, act) == (gcat, gact):
                    continue
                y = YIELDS.get((gc if plat == "macOS" else gc.replace("Ctrl+", "Cmd+", 1), cat))
                if y is None:
                    bad.append(
                        f"[{plat}] {gc}: app-wide '{gact}' and {cat} '{act}' - give one a new key, "
                        f"or make the app step aside and list it in YIELDS"
                    )
                elif y[1] not in (ROOT / y[0]).read_text():
                    bad.append(f"[{plat}] {gc}: YIELDS says {y[0]} steps aside for {cat} ('{y[1]}'), but that code is gone")

        for cat, act, c in rows:
            if act in OS_OWN:
                continue
            if c.lower() in {r.lower() for r in RESERVED[plat]}:
                bad.append(f"[{plat}] {c}: {cat} '{act}' takes a key {plat} keeps for itself")
        counts[plat] = (len(rows), len({r[0] for r in rows}))

    n, areas = counts["macOS"]
    bad = list(dict.fromkeys(bad))
    return n, areas, bad


def check_core_verbs():
    text = SRC_HELP.read_text()
    issues = []
    for m in ROW_RX.finditer(text):
        cat, act, expr = m.group(1), m.group(2), m.group(3)
        mappings = CORE_ACTION_MAP.get(act)
        if not mappings:
            continue
        row_chords = chords(key_text(expr, "macOS"))
        for verb, idx in mappings:
            if (cat, verb) in CORE_VERB_ALLOW:
                continue
            canon = CORE_VERBS[verb]
            if idx is not None:
                if idx >= len(row_chords):
                    issues.append(f"core verb '{verb}' in {cat} ('{act}'): missing chord at index {idx} in {row_chords}")
                    continue
                actual = [row_chords[idx]]
            else:
                actual = row_chords
            # Normalize Shift+Cmd+G -> Cmd+Shift+G for comparison
            norm = lambda c: "+".join(sorted(c.split("+")[:-1], key=lambda x: MODS.index(x) if x in MODS else 99) + [c.split("+")[-1]]).lower()
            if norm(canon) not in [norm(a) for a in actual]:
                issues.append(f"core verb '{verb}' in {cat} ('{act}'): has {actual}, expected {canon}")
    return issues


def check_panel_focus():
    """Verify every panel file's key handlers sit behind a focus/hover/modal guard."""
    # Each panel area's primary focus/hover guard symbol(s)
    PANEL_GUARDS = {
        "ArrangePanel.cpp": "gArrangeFocused",
        "ArrangeSessionGrid.cpp": "gSessionClaimedKeys",
        "ChatPanel.cpp": "sInputWasActive",
        "CurveEditorPanel.cpp": "gCurveEditorClaimedKeys",
        "FieldPatcherPanel.cpp": "gFieldPatcherClaimedKeys",
        "FindOverlay.cpp": "sOpen",
        "MixerPanel.cpp": "IsWindowHovered",
        "PerfPanel.cpp": "gPerfMatrixFocused",
        "PianoRollPanel.cpp": "gPianoRollClaimedKeys",
        "SampleStudioPanel.cpp": "hovered",
        "TerminalPanel.cpp": "gTerminalFocus",
        "View2DPanel.cpp": "gView2DClaimedKeys",
        "View3DPanel.cpp": "gView3DClaimedKeys",
    }
    key_rx = re.compile(r"(?:IsKey(?:Pressed|Down)|\bpressed|\bkey|\brepeated)\(ImGuiKey_")
    focus_rx = re.compile(
        r"IsWindowFocused|IsWindowHovered|IsItemFocused|IsItemHovered|IsItemActive|"
        r"gArrangeFocused|gPerfMatrixFocused|gSessionClaimedKeys|gTerminalFocus|"
        r"gView2DClaimedKeys|gView3DClaimedKeys|gFieldPatcherClaimedKeys|"
        r"gSampleStudioClaimedKeys|gPianoRollClaimedKeys|gCurveEditorClaimedKeys|"
        r"hovered|focused|hasFocus|BeginPopup|IsPopupOpen|WantTextInput|"
        r"sInputWasActive|DropdownOpen|gPerfMidiLearnIdx|gArrangeScrubbing|"
        r"sOpen|active|HandleKeyboard\(|HandleLayerKeys\(|HandlePixelKeys\(|HandleVectorKeys\(|View2DTest|Escape"
    )
    issues = []
    panels_dir = ROOT / "src/app/panels"
    for p in sorted(panels_dir.glob("*.cpp")):
        text = p.read_text(errors="ignore")
        if p.name in PANEL_GUARDS and PANEL_GUARDS[p.name] not in text:
            issues.append(f"panel focus: {p.name} missing expected focus/hover guard '{PANEL_GUARDS[p.name]}'")
        lines = text.splitlines()
        # Track brace-delimited scopes and function-level early-return focus guards
        stack = []  # (depth, header_text)
        func_guarded_depth = None
        depth = 0
        for idx, line in enumerate(lines, 1):
            stripped = line.strip()
            if stripped.startswith("//"):
                continue
            # Detect early-return guard at function body level (depth >= 1)
            if "return;" in stripped and idx >= 2:
                guard_ctx = " ".join(lines[max(0, idx - 4):idx])
                if focus_rx.search(guard_ctx) and "!" in guard_ctx:
                    if func_guarded_depth is None or depth < func_guarded_depth:
                        func_guarded_depth = depth
            if key_rx.search(line):
                # Check if inside a guarded scope, after a function-level early return, on a multi-line if, or in a gated helper
                prev_ctx = " ".join(lines[max(0, idx - 3):idx])
                active = " ".join(h for _, h in stack) + " " + prev_ctx
                in_guarded_func = func_guarded_depth is not None and depth >= func_guarded_depth
                in_gated_helper = (
                    (p.name == "TerminalPanel.cpp" and any("HandleKeyboard" in h for _, h in stack))
                    or (p.name == "View2DPanel.cpp" and any(fn in h for _, h in stack for fn in ("HandleLayerKeys", "HandlePixelKeys", "HandleVectorKeys")))
                )
                if not focus_rx.search(active) and not in_guarded_func and not in_gated_helper:
                    issues.append(f"ungated panel key in {p.name}:{idx}: {stripped[:90]}")
            opens = line.count("{")
            closes = line.count("}")
            if opens > 0:
                hdr = " ".join(lines[max(0, idx - 6):idx])
                for _ in range(opens):
                    depth += 1
                    stack.append((depth, hdr))
            for _ in range(closes):
                if stack:
                    stack.pop()
                depth = max(0, depth - 1)
                if func_guarded_depth is not None and depth < func_guarded_depth:
                    func_guarded_depth = None
    return len(PANEL_GUARDS), issues


def check_settings_keys(kshortcuts_rows):
    """Verify Settings > Keys renders every kShortcuts row (and vice versa) via DrawShortcutsTableBody."""
    issues = []
    help_text = SRC_HELP.read_text()
    settings_path = ROOT / "src/app/panels/SettingsWindow.cpp"
    settings_text = settings_path.read_text()

    if "void DrawShortcutsTableBody(" not in help_text or "for (const auto& s : kShortcuts)" not in help_text:
        issues.append("HelpWindows.cpp missing DrawShortcutsTableBody iterating kShortcuts")
    if 'DrawShortcutsTableBody("win_")' not in help_text:
        issues.append("DrawShortcutsWindow does not delegate to DrawShortcutsTableBody")
    if 'I18N_KEY("Keys")' not in settings_text:
        issues.append("SettingsWindow.cpp missing 'Keys' tab in kTabs")
    if "DrawShortcutsTableBody(" not in settings_text:
        issues.append("SettingsWindow.cpp > Keys does not render DrawShortcutsTableBody(kShortcuts)")

    settings_rows = kshortcuts_rows if not issues else 0
    return kshortcuts_rows, settings_rows, issues


def check_drift(quiet=False):
    src, locate = appsrc.read_indexed(str(ROOT))
    tbl = re.search(r"static const ShortcutEntry kShortcuts\[\] = \{(.*?)\n      \};", src, re.S)
    if not tbl:
        return None, ["could not find kShortcuts table in src/"]

    S = r'(?:I18N_KEY\(\s*)?"([^"]*)"\s*\)?'
    rows = re.findall(r'\{\s*' + S + r'\s*,\s*' + S + r'\s*,\s*([^,]+?)\s*,\s*' + S + r'\s*\}', tbl.group(1))

    documented = {}
    unmappable = []
    for cat, action, keyexpr, _desc in rows:
        literal = " ".join(re.findall(r'"([^"]*)"', keyexpr))
        if "MODKEY" in keyexpr:
            literal = "Mod " + literal
        found = False
        alts = [literal] if literal.strip() == "/" else re.split(r"\s*/\s*", literal)
        for alt in alts:
            for tok in re.split(r"\+", alt):
                k = norm_key(tok)
                if k:
                    documented.setdefault(k, []).append(f"{cat}: {action}")
                    found = True
        if not found:
            unmappable.append(f"{cat}: {action} ({literal.strip()})")

    handlers = {}
    for m in re.finditer(r"(?:IsKey(?:Pressed|Down)|\bpressed|\bkey|\brepeated)\(ImGuiKey_([A-Za-z0-9]+)", src):
        k = m.group(1)
        line = src.count("\n", 0, m.start()) + 1
        handlers.setdefault(k, []).append(line)

    super_only = []
    for i, line in enumerate(src.splitlines(), 1):
        stripped = line.strip()
        if stripped.startswith("//") or stripped.startswith("*"):
            continue
        if not re.search(r"KeySuper|ImGuiMod_Super", line):
            continue
        if re.search(r"KeyCtrl|ImGuiMod_Ctrl", line):
            continue
        if any(a in stripped for a in SUPER_ALLOW):
            continue
        super_only.append((i, stripped))

    undocumented = sorted(k for k in handlers if k not in documented and k not in LOCAL_KEYS)
    unhandled = sorted(k for k in documented if k not in handlers)

    issues = []
    for k in unhandled:
        issues.append(f"unhandled key {k}: documented in {'; '.join(documented[k])}")
    for k in undocumented:
        issues.append(f"undocumented key {k}: handled at {', '.join(locate(n) for n in handlers[k])}")
    for ln, text in super_only:
        issues.append(f"super-only (macOS-only risk) at {locate(ln)}: {text[:110]}")

    stats = {
        "rows": len(rows),
        "handled": len(handlers),
        "unhandled": len(unhandled),
        "undocumented": len(undocumented),
        "super_only": len(super_only),
    }
    return stats, issues


# ---------------------------------------------------------------------------
# 6. Menu text: the keys a menu row prints must be keys the table gives that action, and never an OS key
#    (Sculpt Mode printed Cmd+Tab while the table said Ctrl+Tab, 2026-11-17).
# ---------------------------------------------------------------------------
MENU_RX = re.compile(r'MenuParts::Item\(\s*[TL]\("([^"#]*)(?:##[^"]*)?"\)\s*,\s*((?:[^,()]|\([^()]*\))*)')
KEYS_IN_ARG = re.compile(r'((?:(?:MODKEY|TERMKEY)\s*)?"(?:[^"\\]|\\.)*")')
# Which table area a file's menus belong to. Files not listed are only checked for OS keys.
MENU_AREA = {
    "View3DPanel.cpp": ["3D View"], "View2DPanel.cpp": ["2D View"], "CurveEditorPanel.cpp": ["Curve Editor"],
    "PianoRollPanel.cpp": ["Piano Roll"], "ArrangePanel.cpp": ["Arrangement Timeline"], "PerfPanel.cpp": ["Session"],
    "ArrangeSessionGrid.cpp": ["Session"], "SampleStudioPanel.cpp": ["Sample Studio"], "DrumPanel.cpp": ["Drums"],
    "FieldPatcherPanel.cpp": ["Field Patcher"], "TerminalPanel.cpp": ["Terminal"], "ChatPanel.cpp": ["Chat"],
    "MixerPanel.cpp": ["Mixer"],
}
KEY_SAME = {"del": "delete", "bksp": "backspace", "esc": "escape", "return": "enter", "option": "alt"}


def chord_id(c):
    parts = [KEY_SAME.get(x.strip().lower(), x.strip().lower()) for x in c.split("+")]
    return (frozenset(parts[:-1]), parts[-1])


def check_menu_text():
    text = SRC_HELP.read_text()
    table = {}   # (area, action word) -> {chord ids}
    for m in ROW_RX.finditer(text):
        ids = {chord_id(c) for c in chords(key_text(m.group(3)))}
        for word in m.group(2).split(" / "):
            table.setdefault((m.group(1), word.strip().lower()), set()).update(ids)
    bad, n = [], 0
    for f in sorted((ROOT / "src/app").rglob("*.cpp")):
        src = f.read_text(errors="ignore")
        areas = MENU_AREA.get(f.name, [])
        for m in MENU_RX.finditer(src):
            label, arg = m.group(1), m.group(2)
            line = src.count("\n", 0, m.start()) + 1
            for k in KEYS_IN_ARG.findall(arg):
                if re.search(r"Sc\(\s*" + re.escape(k), arg):
                    k = k.replace('"Ctrl', 'MODKEY "').replace('+Ctrl', '+" MODKEY "')   # Sc() prints Ctrl as Cmd on macOS
                shown = key_text(k)
                if not re.search(r"[A-Za-z0-9]", shown) or re.search(r"Click|Drag|Scroll", shown):
                    continue
                n += 1
                for plat in PLATFORMS:
                    c = key_text(k, plat)
                    if c.lower() in {r.lower() for r in RESERVED[plat]} and label not in OS_OWN:
                        bad.append(f"{f.relative_to(ROOT)}:{line}: menu '{label}' shows {c}, a key {plat} keeps for itself")
                for area in areas:
                    want = None
                    for (ar, word), ids in table.items():
                        if ar == area and (word == label.lower() or word == label.lower() + " item" or word == label.lower() + " layer"):
                            want = ids
                    if want is not None and not {chord_id(c) for c in chords(shown)} & want:
                        bad.append(f"{f.relative_to(ROOT)}:{line}: menu '{label}' shows {shown}; {area}'s table row says otherwise")
    return n, list(dict.fromkeys(bad))


def main():
    quiet = "--quiet" in sys.argv
    n_chords, areas, overlap_bad = check_overlap_and_os()
    verb_bad = check_core_verbs()
    n_panels, focus_bad = check_panel_focus()
    stats, drift_bad = check_drift(quiet=quiet)
    k_rows, s_rows, settings_bad = check_settings_keys(stats["rows"] if stats else 0)
    n_menu, menu_bad = check_menu_text()

    total_bad = len(overlap_bad) + len(verb_bad) + len(focus_bad) + len(drift_bad) + len(settings_bad) + len(menu_bad)
    if not quiet:
        print(f"keys gate [overlap/os]: {n_chords} chords in {areas} areas x {len(PLATFORMS)} OS, {len(overlap_bad)} problem(s)")
        for b in overlap_bad:
            print(" ", b)
        print(f"keys gate [core verbs]: {len(CORE_VERBS)} verbs checked, {len(verb_bad)} problem(s)")
        for v in verb_bad:
            print(" ", v)
        print(f"keys gate [panel focus]: {n_panels} panel areas checked, {len(focus_bad)} problem(s)")
        for f in focus_bad:
            print(" ", f)
        print(f"keys gate [settings]: kShortcuts={k_rows} rows, Settings>Keys={s_rows} rows, {len(settings_bad)} problem(s)")
        for sb in settings_bad:
            print(" ", sb)
        print(f"keys gate [menu text]: {n_menu} menu keys checked, {len(menu_bad)} problem(s)")
        for mb in menu_bad:
            print(" ", mb)
        if stats:
            print(
                f"keys gate [drift]: {stats['rows']} documented rows, {stats['handled']} handled keys, "
                f"{stats['unhandled']} unhandled, {stats['undocumented']} undocumented, "
                f"{stats['super_only']} super-only lines"
            )
        for d in drift_bad:
            print(" ", d)
    else:
        rows = stats["rows"] if stats else 0
        handled = stats["handled"] if stats else 0
        print(
            f"keys gate: {n_chords} chords ({areas} areas x {len(PLATFORMS)} OS), "
            f"{rows} rows, {handled} handled keys, {total_bad} problem(s): "
            f"{'OK' if total_bad == 0 else 'FAIL'}"
        )

    return 0 if total_bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
