#!/usr/bin/env python3
"""Keys lint: no shortcut means two things at once.

Reads the one shortcut table (src/app/panels/HelpWindows.cpp kShortcuts, the All Shortcuts window) and checks:
  1. inside one area (category), a chord does one thing;
  2. an app-wide chord (works whatever has the pointer) is not also a panel's chord, unless that panel takes the key
     while the pointer is on it and the app-wide handler yields. Each such yield is listed in YIELDS with the code that
     makes it, and the lint checks that code is still there.
  3. both run for each OS (MODKEY is Cmd on macOS, Ctrl on Windows and Linux; TERMKEY is Cmd / Ctrl+Shift), so a
     table row that writes Ctrl itself can't land on a MODKEY row on Windows only;
  4. no row takes a key the OS keeps for itself (RESERVED), unless it is the app's own use of that key (OS_OWN).
Exit 1 on any hit. Add a row to the table first; the lint then tells you if its key is free.
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "src/app/panels/HelpWindows.cpp"

# App-wide rows: they fire wherever the pointer is.
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

ROW = re.compile(r'\{\s*I18N_KEY\("([^"]*)"\),\s*I18N_KEY\("([^"]*)"\),\s*((?:MODKEY|TERMKEY|"(?:[^"\\]|\\.)*"|\s)*),\s*I18N_KEY\(')
MODS = ("Cmd", "Ctrl", "Shift", "Alt", "Option")
NAV = {"Up", "Down", "Left", "Right", "PageUp", "PageDown", "Home", "End"}
# Rows that only act in a mode or state of their area (Fly mode, Pose mode, a focused param, a picture open): their keys
# may repeat the area's other keys, because the two never apply at once.
MODE_ACTIONS = {"Fly", "Turn / Clear Bone", "Edit Operations", "Pose Mode", "Sculpt Mode", "Nudge Param", "Close Picture"}


PLATFORMS = {"macOS": {"MODKEY": "Cmd", "TERMKEY": "Cmd"},
             "Windows": {"MODKEY": "Ctrl", "TERMKEY": "Ctrl+Shift"},
             "Linux": {"MODKEY": "Ctrl", "TERMKEY": "Ctrl+Shift"}}

# Keys the OS or desktop takes before the app sees them (or that users expect to mean one thing everywhere).
RESERVED = {
    "macOS": {"Cmd+Tab", "Cmd+Space", "Cmd+H", "Cmd+M", "Cmd+Q", "Cmd+`", "Cmd+Alt+Esc", "Cmd+Shift+3", "Cmd+Shift+4",
              "Cmd+Shift+5", "Ctrl+Up", "Ctrl+Down", "Ctrl+Left", "Ctrl+Right", "Ctrl+Cmd+Q", "Cmd+Alt+H", "Cmd+Alt+D"},
    "Windows": {"Alt+F4", "Alt+Tab", "Alt+Space", "Alt+Esc", "Ctrl+Esc", "Ctrl+Alt+Delete", "Ctrl+Shift+Esc",
                "Ctrl+Alt+Del", "Win+D", "Win+L", "Win+E", "Win+Tab"},
    "Linux": {"Alt+F4", "Alt+Tab", "Alt+F2", "Alt+Space", "Alt+F7", "Alt+F8", "Ctrl+Alt+T", "Ctrl+Alt+Delete",
              "Ctrl+Alt+Del", "Ctrl+Alt+Left", "Ctrl+Alt+Right", "Ctrl+Alt+Up", "Ctrl+Alt+Down"},
}
# The app's own use of a reserved key is fine: Quit is Cmd+Q, Hide is Cmd+H.
OS_OWN = {"Quit", "Hide", "Minimize", "Minimise"}


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
            out.append(prefix + piece)   # 'Alt+Left / Right'; a letter never inherits: write 'Cmd+T / Cmd+W'
        else:
            prefix = ""
            out.append(piece)
    # Only real key chords: drags, clicks, wheels and ranges are gestures, not keys.
    return [c for c in out if not re.search(r"Drag|Click|Scroll|Wheel|\.\.\.|Hold", c)]


text = SRC.read_text()
bad, counts = [], {}
for plat in PLATFORMS:
    rows = []
    for m in ROW.finditer(text):
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
            # YIELDS is written with macOS's Cmd; MODKEY rows read Ctrl elsewhere.
            y = YIELDS.get((gc if plat == "macOS" else gc.replace("Ctrl+", "Cmd+", 1), cat))
            if y is None:
                bad.append(f"[{plat}] {gc}: app-wide '{gact}' and {cat} '{act}' - give one a new key, or make the app step aside and list it in YIELDS")
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
print(f"keys lint: {n} chords in {areas} areas x {len(PLATFORMS)} OS, {len(bad)} problem(s)")
for b in bad:
    print(" ", b)
sys.exit(1 if bad else 0)
