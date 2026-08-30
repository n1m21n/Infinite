#!/usr/bin/env python3
"""Static cross-check of Infinite's keyboard shortcuts, both platforms at once.

Three questions, none of which need a build or a running app:

  1. Documented but unhandled - every row of the `kShortcuts` table in
     DrawShortcutsWindow (src/main.cpp) names a key. Is there a real
     ImGui::IsKeyPressed handler for that key anywhere in main.cpp?
  2. Handled but undocumented - the reverse. A binding nobody can discover is
     a binding that does not exist for most users.
  3. macOS-only modifier - a handler gated on io.KeySuper (or ImGuiMod_Super)
     without the matching KeyCtrl/ImGuiMod_Ctrl is dead on Windows. The app's
     convention is `const bool cmdOrCtrl = io.KeyCtrl || io.KeySuper;`.

Exit code 0 if nothing is reported, 1 otherwise. --quiet prints only the
verdict line.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
MAIN = os.path.join(ROOT, "src", "main.cpp")

KEY_ALIASES = {
    "delete": "Delete", "backspace": "Backspace", "space": "Space",
    "/": "Slash", "escape": "Escape", "enter": "Enter", "tab": "Tab",
    "scroll wheel": None, "drag canvas": None, "shift + drag": None,
}

def norm_key(tok):
    """One key token from the help table -> the ImGuiKey_ suffix it should map to."""
    t = tok.strip().lower()
    if t in KEY_ALIASES:
        return KEY_ALIASES[t]
    if len(t) == 1 and t.isalpha():
        return t.upper()
    if len(t) == 1 and t == "/":
        return "Slash"
    return None

# Keys handled in main.cpp that are deliberately NOT global bindings, so their
# absence from the help window is correct rather than a documentation gap.
# Adding to this list is a deliberate act: it asserts "this key is local to one
# dialog/popup and no user needs to discover it from the shortcut list".
LOCAL_KEYS = {
    "Escape": "dismisses a popup/modal/inline edit - not a global binding",
    "Enter":  "commits an inline text edit - not a global binding",
    "Minus":  "zoom-out within the canvas zoom handler, paired with the scroll gesture",
}

# Lines that mention Super without Ctrl and are known-correct. Matched by
# content, not line number, so the list survives edits elsewhere in the file.
SUPER_ALLOW = [
    # The synthetic input driver for INFINITE_INPUTTEST injects a macOS Cmd+C/V.
    # It exercises the real handler, which accepts Ctrl too; the fixture itself
    # only ever runs on the host it was written for.
    "key(ImGuiMod_Super, true); key(ImGuiKey_C, true);",
    "key(ImGuiKey_V, false); key(ImGuiMod_Super, false);",
]


def main():
    quiet = "--quiet" in sys.argv
    src = open(MAIN, encoding="utf-8", errors="replace").read()

    # ---- 1. the documented table -------------------------------------------
    tbl = re.search(r"static const ShortcutEntry kShortcuts\[\] = \{(.*?)\n      \};", src, re.S)
    if not tbl:
        print("SHORTCUTSWEEP FAIL - could not find the kShortcuts table in src/main.cpp")
        return 1
    rows = re.findall(r'\{\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*([^,]+?)\s*,\s*"([^"]*)"\s*\}', tbl.group(1))

    documented = {}   # ImGuiKey suffix -> [action, ...]
    unmappable = []
    for cat, action, keyexpr, _desc in rows:
        # keyexpr is either "..." or MODKEY "..." or a concatenation
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

    # ---- 2. the real handlers ----------------------------------------------
    handlers = {}     # ImGuiKey suffix -> [line numbers]
    for m in re.finditer(r"IsKeyPressed\(ImGuiKey_([A-Za-z0-9]+)", src):
        k = m.group(1)
        line = src.count("\n", 0, m.start()) + 1
        handlers.setdefault(k, []).append(line)

    # ---- 3. macOS-only modifier gates --------------------------------------
    # Any line mentioning Super without Ctrl on the same line, excluding the
    # canonical `io.KeyCtrl || io.KeySuper` idiom which is already both.
    super_only = []
    for i, line in enumerate(src.splitlines(), 1):
        stripped = line.strip()
        if stripped.startswith("//") or stripped.startswith("*"):
            continue  # a comment naming the modifier is not a gate
        if not re.search(r"KeySuper|ImGuiMod_Super", line):
            continue
        if re.search(r"KeyCtrl|ImGuiMod_Ctrl", line):
            continue
        if any(a in stripped for a in SUPER_ALLOW):
            continue
        super_only.append((i, stripped))

    undocumented = sorted(k for k in handlers if k not in documented and k not in LOCAL_KEYS)
    local = sorted(k for k in handlers if k in LOCAL_KEYS)
    unhandled = sorted(k for k in documented if k not in handlers)

    if not quiet:
        print("=== documented in kShortcuts but no IsKeyPressed handler ===")
        for k in unhandled:
            print(f"  {k:12} <- {'; '.join(documented[k])}")
        if not unhandled:
            print("  (none)")

        print("\n=== handled but absent from the kShortcuts help table ===")
        for k in undocumented:
            print(f"  {k:12} main.cpp:{','.join(str(n) for n in handlers[k])}")
        if not undocumented:
            print("  (none)")

        print("\n=== dialog-local keys, allowlisted (informational) ===")
        for k in local:
            print(f"  {k:12} {LOCAL_KEYS[k]}")
        if not local:
            print("  (none)")

        print("\n=== rows whose key is a gesture, not a key (informational) ===")
        for u in unmappable:
            print(f"  {u}")

        print("\n=== Super without Ctrl on the same line (macOS-only risk) ===")
        for ln, text in super_only:
            print(f"  main.cpp:{ln}: {text[:110]}")
        if not super_only:
            print("  (none)")

    bad = len(unhandled) + len(undocumented) + len(super_only)
    print(f"\nSHORTCUTSWEEP {len(rows)} documented rows, {len(handlers)} handled keys, "
          f"{len(unhandled)} unhandled, {len(undocumented)} undocumented, "
          f"{len(super_only)} super-only lines: {'OK' if bad == 0 else 'FAIL'}")
    return 0 if bad == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
