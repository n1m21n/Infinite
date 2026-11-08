#!/usr/bin/env python3
"""Plugin-mode audit: every user-facing UI entry point, and whether a PluginModeActive guard covers it.

Scans src/app and src/arrange for menus, Settings tabs and sections, buttons and tiles, keyboard shortcuts,
drop handlers and the node spawn list. For each item it reports whether a `Platform::PluginModeActive()`
check covers it: on the item's own statement, in an enclosing `if` (or its `else`), as an early
return/continue earlier in the block, at every call site of the function or lambda it sits in, or in the
function its statement calls (a function that returns early in plugin mode). A local `bool x = ...
PluginModeActive...` (or a lambda that tests it) counts as the guard wherever `x` is tested, and a
Settings tab body (`if (sTab == N)`) counts as guarded when the tab is hidden in plugin mode.

Each item is then matched against CLASSES below (first match wins):
  works     works inside a DAW, keep
  host      the DAW owns it: must be hidden or read-only (guarded)
  deferred  deferred or broken inside a DAW: must be hidden (guarded)
  debug     #ifndef NDEBUG only, never in a release build
  unreachable  sits in a panel or popup that plugin mode never opens (reason says why)

  python3 tools/plugin/plugin_mode_audit.py            # table of problems (host/deferred unguarded, unclassified)
  python3 tools/plugin/plugin_mode_audit.py --all      # every item
  python3 tools/plugin/plugin_mode_audit.py --md FILE  # write the full markdown table (keeps FILE's text above "## Table")
Exit code 1 when a host/deferred item is unguarded or an item has no class.
"""
import argparse
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCAN_DIRS = ["src/app", "src/arrange"]
CALLSITE_DIRS = ["src/app", "src/arrange", "src/plugin/engine", "src/nodes"]
CALLSITE_FILES = ["src/main.cpp"]
GUARD = "PluginModeActive"

# Kinds of entry points: (kind, regex with the label in group "label").
LABEL = r'\s*(?:L|T)?\(?\s*"(?P<label>[^"]*)"'
PATTERNS = [
    ("menu", re.compile(r"MenuParts::(?:Item|Check|SubMenu)\(" + LABEL)),
    ("menu", re.compile(r"ImGui::(?:MenuItem|BeginMenu)\(" + LABEL)),
    ("section", re.compile(r"FormParts::Section\(" + LABEL)),
    ("button", re.compile(r"(?:FormParts::Button|ActionButton::Draw|ChipButton::Draw|IconTile::Draw|ImGui::Button|ImGui::SmallButton)\(" + LABEL)),
    ("switch", re.compile(r"FormParts::(?:Switch|Checkbox)\(" + LABEL)),
    ("shortcut", re.compile(r"IsKey(?:Chord)?Pressed\(\s*(?P<label>(?:ImGuiMod_\w+\s*\|\s*)*ImGuiKey_\w+)")),
    ("drop", re.compile(r"HasExtension\(\s*\w+\s*,\s*(?P<label>[\w\"\.]+)\s*\)")),
    ("rail", re.compile(r"PanelRail::Item\(" + LABEL)),
]

# (file regex, label regex, kind regex or None, class, reason). First match wins; keep specific rules first.
CLASSES = [
    # ---- top bar and menus (StageMenuBar) ----
    (r"StageMenuBar", r"^##transport(play|rewind)$", None, "host", "the DAW's transport plays and stops the plugin"),
    (r"StageMenuBar", r"^(Stop Audio|Start Audio)$", None, "host", "the DAW owns the audio device"),
    (r"StageMenuBar", r"^##metronomeBtn$|^accent first beat$", None, "host", "the DAW's click; ours would mix into the track and its bounces"),
    (r"StageMenuBar", r"^##(update|popout|picture|slots)", None, "works", "plugin-only tiles or update badge (guarded)"),
    (r"StageMenuBar", r"^(Timeline|Session|Studio)$", None, "works", "which driver feeds the plugin's output; works under host transport"),
    (r"StageMenuBar", r"^(Check for updates|Quit)$", None, "host", "the app updates; the DAW closes the plugin"),
    (r"StageMenuBar", r"^(UI Debugger / Item Picker|UI Style Editor)$", None, "debug", "#ifndef NDEBUG"),
    (r"StageMenuBar", r"^Terminal$|^Show Terminal$|^##railTerminal$", None, "deferred", "no agent Terminal inside a DAW (8.10)"),
    (r"StageMenuBar", r"^Share Patch\.\.\.$", None, "works", "uploads the patch text; no app state involved"),
    (r"StageMenuBar", r"^(Show lock|Resume after panic|Panic|Pre-show check|Next patch|Previous patch|Show Set|Show Set window)$", None, "works", "show tools act on the patch and the engine's own output"),
    (r"StageMenuBar", r".", None, "works", "File/Edit/View items act on the patch, which the DAW saves in its project"),
    # ---- Settings ----
    (r"SettingsWindow", r"^Audio$", "settings tab", "host", "the DAW owns the audio device and MIDI inputs"),
    (r"SettingsWindow", r"^Agents & Terminal$", "settings tab", "deferred", "no agent Terminal inside a DAW (8.10)"),
    (r"SettingsWindow", r"^Plugins$|^Scanned plugins$", None, "deferred", "no plugin hosting inside a DAW"),
    (r"SettingsWindow", r"^Audio Devices & Format$", None, "host", "the DAW owns the device, rate and buffer"),
    (r"SettingsWindow", r"^(Output device|Input device|Sample rate|Buffer size|Apply|Apply##.*)$", None, "host", "the DAW owns the device"),
    (r"SettingsWindow", r"^(MIDI Inputs|Start MIDI)$", None, "host", "MIDI comes from the DAW track"),
    (r"SettingsWindow", r"^(Application & Updates|Check on launch|Check for updates\.\.\.)$", None, "host", "the app updates; the plugin follows"),
    (r"SettingsWindow", r"^Infinite$|^Update$", None, "host", "app update offer on the Extensions page"),
    (r"SettingsWindow", r"^(Extensions|Check for updates|Install from file\.\.\.|Install|Update|Cancel)$", None, "works", "packs install into the shared folder the app reads too"),
    (r"SettingsWindow", r"^(Graphics processor)$", None, "works", "read at engine start"),
    (r"SettingsWindow", r"^Reference$", None, "deferred", "Terminal / CLI reference; no Terminal inside a DAW"),
    (r"SettingsWindow", r"^(Autosave|Autosave enabled)$", None, "host", "the DAW saves the patch in its project; engine autosave never runs in plugin mode"),
    (r"SettingsWindow", r".", None, "works", "look, canvas, field and globals settings act on the engine's own UI"),
    (r"CliSetupSection", r".", None, "unreachable", "drawn only on the Agents & Terminal Settings tab, hidden in plugin mode"),
    (r"DawPluginSection", r".", None, "host", "the app installs the plugin"),
    # ---- side rail, panels ----
    (r"StageSidePanels", r"(?i)terminal|chat", None, "deferred", "no agent Terminal or Chat inside a DAW (8.10)"),
    (r"ChatPanel|TerminalPanel|TerminalDraw", r".", None, "unreachable", "inside the Terminal/Chat panel: gTerminalOpen is forced off on load and every opener is guarded"),
    (r"ArrangeRender", r"^(Cancel|Cancel All)$", None, "unreachable", "render progress window; no render can start in plugin mode (every Render entry is hidden)"),
    (r"ArrangeRender", r"(?i)^render", None, "deferred", "the DAW bounces; Arrange render is refused in the engine"),
    (r"ArrangeRender", r".", None, "works", "clip, track and group editing in the Timeline"),
    (r"ArrangePanel|ArrangeSession", r"^(Render|Render Track|Render Group|Render\.\.\.|Export.*)$", None, "deferred", "the DAW bounces"),
    (r"ArrangePanel|ArrangeSession", r"^##(arrangeplaybtn|arrangerewindbtn|sessionplay)$", None, "host", "the DAW's transport plays; LaunchRequest* is a no-op in plugin mode"),
    (r"ArrangePanel", r"^##arrangeloopbtn$", None, "host", "Transport::FollowHost suspends the Arrange loop; the DAW loops"),
    (r"ArrangePanel|ArrangeSession", r"^(ImGuiKey_Space|ImGuiKey_Enter|ImGuiKey_KeypadEnter|ImGuiKey_Home)$", "shortcut", "works", "routes to LaunchRequest*, a no-op in plugin mode"),
    (r"ArrangePanel|ArrangeSession", r".", None, "works", "clips and lanes edit the patch; playback follows the host transport"),
    (r"View3DPanel", r"^Render Animation", None, "deferred", "the DAW bounces; films are made in the app"),
    (r"View3DPanel", r".", None, "works", "3D View edits the patch"),
    (r"SampleStudioPanel", r".", None, "works", "records from Host main; results leave as nodes or files"),
    (r"PianoRollPanel", r".", None, "works", "edits notes; plays under host transport"),
    (r"HostSlots", r".", None, "works", "plugin-only host automation"),
    (r"StageDropHandling", r"kPluginBundleExt", None, "deferred", "no plugin hosting inside a DAW"),
    (r"StageDropHandling", r".", None, "works", "patch, media and sample drops load into the engine"),
    (r"StageKeyboard", r"^ImGuiKey_(Space|Enter|KeypadEnter)$", None, "works", "routes to LaunchRequest*, a no-op in plugin mode"),
    (r"StageKeyboard", r"ImGuiKey_GraveAccent", None, "deferred", "opens the Terminal"),
    (r"StageKeyboard", r".", None, "works", "canvas editing keys"),
    (r"bodies/", r"^Render(\.\.\.)?$|^Render##", None, "deferred", "the DAW bounces"),
    (r"bodies/AudioNodeBodies", r"^(Record##|Stop##|Choose\.\.\.##|WAV$|FLAC$|MP3$|24 bit##)", None, "host", "the DAW records the plugin's output"),
    (r"bodies/", r".", None, "works", "node bodies act on the node; inputs come from the host"),
    (r"StageNodeBodies", r"^(Record video|Stop recording|Finalizing\.\.\.)$", None, "deferred", "live video recording is Out inside a DAW (README features table)"),
    (r"PreviewBodies|StageNodeBodies", r".", None, "works", "node bodies act on the node"),
    (r"StagePopupsA", r"^Reference in chat$", None, "deferred", "no Chat inside a DAW (8.10)"),
    (r"UiAudit|UiGallery", r".", None, "debug", "design review tools, opened only by INFINITE_* review hooks"),
    (r"Diagnostics", r".", None, "works", "copies system info / reveals logs"),
    (r"Startup|main\.cpp", r".", None, "works", "startup"),
    (r".", r".", None, "works", "acts on the patch or the engine's own UI"),
]

CONTROL = re.compile(r"^\s*(if|else|for|while|switch|do|try|catch|return|case)\b")
FUNC_HDR = re.compile(r"([A-Za-z_~][\w:~]*)\s*\([^;{}]*\)\s*(?:const)?\s*(?:->\s*[\w:<>&\*\s]+)?\s*$")
LAMBDA_HDR = re.compile(r"(?:auto|std::function<[^>]*>)\s*&?\s*(\w+)\s*=\s*\[")


def strip_code(line, state):
    """Remove comments and string/char literals. state['block'] tracks /* */ across lines."""
    out = []
    i = 0
    n = len(line)
    while i < n:
        if state["block"]:
            j = line.find("*/", i)
            if j < 0:
                return "".join(out)
            state["block"] = False
            i = j + 2
            continue
        c = line[i]
        if line.startswith("//", i):
            break
        if line.startswith("/*", i):
            state["block"] = True
            i += 2
            continue
        if c == '"' or c == "'":
            q = c
            j = i + 1
            while j < n and line[j] != q:
                j += 2 if line[j] == "\\" else 1
            out.append(q + q)
            i = j + 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


class Frame:
    __slots__ = ("header", "guard", "name", "early", "nsish")

    def __init__(self, header, guard, name, nsish):
        self.header = header
        self.guard = guard
        self.name = name
        self.early = False
        self.nsish = nsish


ALIAS_RE = re.compile(r"\b(?:const\s+)?(?:bool|auto)\s+(\w+)\s*=[^;]*" + GUARD)
GUARDED_FUNCS = set()   # functions whose body returns early in plugin mode


def hidden_settings_tabs():
    """Indexes of the Settings tabs plugin mode skips (from the hiddenInDaw lambda / kPluginsTab)."""
    try:
        src = open(os.path.join(ROOT, "src/app/panels/SettingsWindow.cpp"), encoding="utf-8").read()
    except OSError:
        return set()
    out = set()
    m = re.search(r"hiddenInDaw\s*=\s*\[[^\]]*\]\s*\([^)]*\)\s*\{([^}]*)\}", src)
    if m and GUARD in m.group(1):
        out |= set(int(x) for x in re.findall(r"tab\s*==\s*(\d+)", m.group(1)))
    for name_const in re.findall(r"PluginModeActive\(\)\s*&&\s*i\s*==\s*(k\w+)", src):
        mm = re.search(r"constexpr int " + name_const + r"\s*=\s*(\d+)", src)
        if mm:
            out.add(int(mm.group(1)))
    return out


def analyse(path):
    """Per line: (guarded, innermost function/lambda name, in NDEBUG-only block)."""
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    raw = text.split("\n")
    # The guard, plus local aliases of it (`const bool canRecord = !Platform::PluginModeActive();`).
    words = [GUARD] + sorted(set(ALIAS_RE.findall(text)))
    if path.endswith("SettingsWindow.cpp"):
        tabs = hidden_settings_tabs()
        guard_re = re.compile(r"\b(?:" + "|".join(map(re.escape, words)) + r")\b" +
                              (r"|\bsTab\s*==\s*(?:" + "|".join(map(str, sorted(tabs))) + r")\b" if tabs else ""))
    else:
        guard_re = re.compile(r"\b(?:" + "|".join(map(re.escape, words)) + r")\b")
    has = lambda t: bool(guard_re.search(t))
    state = {"block": False}
    stack = []
    info = []
    pending = ""            # code since the last ; { }
    pending_if_guard = 0     # >0: the next statement is the body of a brace-less guarded if
    last_closed = {}         # depth -> guard of the block that just closed (for else)
    pp = []                  # preprocessor stack: True when the branch is NDEBUG-only
    for line in raw:
        s = line.strip()
        if s.startswith("#"):
            if re.match(r"#\s*ifndef\s+NDEBUG", s) or re.match(r"#\s*if\s+!\s*defined\s*\(?\s*NDEBUG", s):
                pp.append(True)
            elif re.match(r"#\s*if", s):
                pp.append(False)
            elif re.match(r"#\s*else", s) and pp:
                pp[-1] = False
            elif re.match(r"#\s*endif", s) and pp:
                pp.pop()
            info.append((False, None, any(pp), ""))
            continue
        code = strip_code(line, state)
        stmt_guard = has(pending + code) and not ALIAS_RE.search(pending + code)
        enclosing = any(fr.guard or fr.early for fr in stack)
        name = None
        for fr in reversed(stack):
            if fr.name:
                name = fr.name
                break
        guarded = enclosing or stmt_guard or pending_if_guard > 0
        info.append((guarded, name, any(pp), code))
        # walk braces and statements
        for ch in code:
            if ch == "{":
                hdr = pending.strip()
                g = has(hdr) and not ALIAS_RE.search(hdr)
                if re.match(r"^\}?\s*else\b", hdr) and len(stack) in last_closed:
                    g = g or last_closed[len(stack)]
                nm = None
                m = LAMBDA_HDR.search(hdr)
                if m:
                    nm = m.group(1)
                elif not CONTROL.match(hdr):
                    m = FUNC_HDR.search(hdr)
                    if m and m.group(1) not in ("if", "for", "while", "switch", "catch"):
                        nm = m.group(1).split("::")[-1]
                nsish = bool(re.match(r"^(namespace|struct|class|enum|union|extern)\b", hdr))
                if pending_if_guard > 0:
                    g = True
                    pending_if_guard = 0
                stack.append(Frame(hdr, g, nm, nsish))
                pending = ""
            elif ch == "}":
                if stack:
                    fr = stack.pop()
                    last_closed[len(stack)] = fr.guard
                pending = ""
            elif ch == ";":
                st = pending.strip()
                if has(st) and re.search(r"\b(return|continue|break)\b", st) and stack:
                    stack[-1].early = True
                    if stack[-1].name and not any(f.name for f in stack[:-1]):
                        GUARDED_FUNCS.add(stack[-1].name)
                if pending_if_guard > 0 and not re.match(r"^\s*(if|else)\b", st):
                    pending_if_guard = 0
                pending = ""
            else:
                pending += ch
        pending += " "
        st = pending.strip()
        # brace-less "if (... PluginModeActive ...)" whose body is on the next line
        if has(st) and re.match(r"^(else\s+)?if\s*\(", st) and st.count("(") == st.count(")"):
            pending_if_guard = 1
    return info, raw


def label_clean(label):
    return label.split("###")[0] if "###" in label else label


def collect():
    files = []
    for d in SCAN_DIRS:
        for dp, _, fs in os.walk(os.path.join(ROOT, d)):
            for f in fs:
                if f.endswith((".cpp", ".mm", ".h", ".inc")):
                    files.append(os.path.join(dp, f))
    callsite_files = set(files)
    for d in CALLSITE_DIRS:
        for dp, _, fs in os.walk(os.path.join(ROOT, d)):
            for f in fs:
                if f.endswith((".cpp", ".mm")):
                    callsite_files.add(os.path.join(dp, f))
    for f in CALLSITE_FILES:
        callsite_files.add(os.path.join(ROOT, f))
    analysed = {p: analyse(p) for p in sorted(callsite_files)}
    return sorted(files), analysed


CALL_RE = re.compile(r"(?<![\w.>:])([A-Za-z_]\w*)\s*\(")
_index = None
_memo = {}


def build_index(analysed):
    """name -> [(guarded, enclosing fn, ndebug, is definition)] for every call-looking token."""
    idx = {}
    for path, (info, _raw) in analysed.items():
        for (g, fn, dbg, code) in info:
            if not code:
                continue
            for m in CALL_RE.finditer(code):
                name = m.group(1)
                pre = code[: m.start()]
                is_def = bool(re.search(r"\b(void|bool|int|float|double|auto|static|inline|std::string|ImVec2)\s*[\w:&\*]*\s*$", pre)) or \
                    bool(re.search(r"auto\s+" + re.escape(name) + r"\s*=", code))
                idx.setdefault(name, []).append((g, fn, dbg, is_def))
            for m in re.finditer(r"auto\s+(\w+)\s*=\s*\[", code):
                idx.setdefault(m.group(1), []).append((g, fn, dbg, True))
    return idx


def callsites_guarded(name, analysed, depth, seen):
    """True when every call of `name` outside its definition is guarded (recursing through callers)."""
    global _index
    if _index is None:
        _index = build_index(analysed)
    if name in _memo:
        return _memo[name]
    if depth > 4 or name in seen:
        return False
    seen = seen | {name}
    found = False
    ok = True
    for (g, fn, dbg, is_def) in _index.get(name, []):
        if is_def:
            continue
        found = True
        if g or dbg:
            continue
        if fn and fn != name and callsites_guarded(fn, analysed, depth + 1, seen):
            continue
        ok = False
        break
    res = found and ok
    if depth == 0:
        _memo[name] = res
    return res


def classify(rel, label, kind):
    for fre, lre, kre, cls, why in CLASSES:
        if re.search(fre, rel) and re.search(lre, label) and (kre is None or re.search(kre, kind)):
            return cls, why
    return None, ""


def node_spawn_items(analysed):
    """Node types offered in the spawn list, from NodeRegistry; the Plugin node's spawn is refused in Core.cpp."""
    out = []
    reg = os.path.join(ROOT, "src/app/graph/NodeRegistry.cpp")
    if not os.path.exists(reg):
        return out
    core = open(os.path.join(ROOT, "src/app/graph/Core.cpp"), encoding="utf-8", errors="replace").read()
    refused = set(re.findall(r'name\s*==\s*"(\w[\w ]*)"\s*&&\s*Platform::PluginModeActive\(\)', core))
    with open(reg, encoding="utf-8", errors="replace") as f:
        for i, line in enumerate(f, 1):
            for m in re.finditer(r'REGISTER_NODE\(\s*\w+\s*,\s*([^,]+?)\s*,\s*"([^"]+)"', line):
                out.append(("node", m.group(1), "src/app/graph/NodeRegistry.cpp", i, m.group(1) in refused))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--all", action="store_true", help="print every item, not only problems")
    ap.add_argument("--md", help="write the full markdown table to this file")
    args = ap.parse_args()

    files, analysed = collect()
    rows = []
    for path in files:
        info, raw = analysed.get(path) or analyse(path)
        rel = os.path.relpath(path, ROOT)
        for ln, line in enumerate(raw, 1):
            code_line = line.split("//")[0]
            for kind, pat in PATTERNS:
                for m in pat.finditer(code_line):
                    label = label_clean(m.group("label"))
                    if not label or label.startswith("%"):
                        continue
                    g, fn, dbg, _ = info[ln - 1]
                    how = "yes" if g else ""
                    if not g and fn and callsites_guarded(fn, analysed, 0, set()):
                        how = "yes (caller)"
                    if not how:
                        # the statement (this line and the next two) calls a function that returns early in plugin mode
                        near = " ".join(info[k][3] for k in range(ln - 1, min(ln + 2, len(info))))
                        if any(re.search(r"\b" + re.escape(f) + r"\s*\(", near) for f in GUARDED_FUNCS):
                            how = "yes (callee)"
                    if dbg:
                        how = how or "debug"
                    rows.append((kind, label, rel, ln, how))
    for kind, label, rel, ln, refused in node_spawn_items(analysed):
        rows.append((kind, label, rel, ln, "yes" if refused else ""))
    # Settings tabs
    sw = os.path.join(ROOT, "src/app/panels/SettingsWindow.cpp")
    src = open(sw, encoding="utf-8").read()
    m = re.search(r"kTabs\[\]\s*=\s*\{(.*?)\};", src, re.S)
    if m:
        tabs = re.findall(r'I18N_KEY\("([^"]+)"\)', m.group(1))
        tab_ln = src[: m.start()].count("\n") + 1
        guarded_tabs = hidden_settings_tabs()
        for idx, t in enumerate(tabs):
            rows.append(("settings tab", t, "src/app/panels/SettingsWindow.cpp", tab_ln, "yes" if idx in guarded_tabs else ""))

    problems = 0
    out_rows = []
    for kind, label, rel, ln, how in rows:
        cls, why = classify(rel, label, kind)
        if how == "debug":
            cls, why = "debug", "#ifndef NDEBUG"
        guarded = how.startswith("yes")
        if cls is None:
            status = "UNCLASSIFIED"
        elif cls in ("host", "deferred") and not guarded and how != "debug":
            status = "FAIL"
        else:
            status = "ok"
        if status != "ok":
            problems += 1
        out_rows.append((status, kind, label, f"{rel}:{ln}", how or "no", cls or "?", why))

    show = out_rows if args.all else [r for r in out_rows if r[0] != "ok"]
    for r in show:
        print(" | ".join(r))
    counts = {}
    for r in out_rows:
        counts[r[5]] = counts.get(r[5], 0) + 1
    print(f"\n{len(out_rows)} items; " + ", ".join(f"{k} {v}" for k, v in sorted(counts.items())) + f"; {problems} problems",
          file=sys.stderr)

    if args.md:
        # Keep the hand-written part of an existing file (everything up to and including "## Table").
        head = ""
        if os.path.exists(args.md):
            old = open(args.md, encoding="utf-8").read()
            k = old.find("\n## Table\n")
            if k >= 0:
                head = old[: k + len("\n## Table\n")] + "\n"
        with open(args.md, "w", encoding="utf-8") as f:
            f.write(head)
            f.write("| Status | Kind | Item | File:line | Guarded | Class | Why |\n|---|---|---|---|---|---|---|\n")
            for r in sorted(out_rows, key=lambda r: (r[3].split(":")[0], int(r[3].split(":")[1]))):
                cells = [c.replace("|", "\\|") for c in r]
                cells[2] = "`" + cells[2] + "`" if cells[1] in ("shortcut", "drop") or cells[2].startswith("##") else cells[2]
                f.write("| " + " | ".join(cells) + " |\n")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
